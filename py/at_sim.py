#!/usr/bin/env python3
"""
模拟从开机到禁止使用网络的AT命令流程
基于 sms_forwarding 项目的 code.ino + modem.cpp + esim.cpp 分析

流程概览:
1. modemPowerCycle() - 硬件操作(EN引脚), 非AT命令
2. modemInit() - 模组初始化:
   - AT (测试通信)
   - ATI (查询模组型号, 决定是否跳过 CGACT)
   - AT+CGACT=0,1 (禁用数据连接)
   - AT+CNMI=2,2,0,0,0 (配置新消息通知)
   - AT+CMGF=0 (设置PDU模式)
   - AT+CEREG? (等待网络注册)
3. esimInit() - eSIM初始化:
   - AT (再次测试)
   - AT+CCHO=? / AT+CCHC=? / AT+CGLA=? (eSIM能力检测)
"""

import sys
import time
import serial
import argparse


def send_at(ser, cmd, timeout=2.0, expect="OK"):
    """发送AT命令并等待响应"""
    print(f"\n>>> {cmd}")
    ser.reset_input_buffer()
    ser.write(f"{cmd}\r\n".encode())

    resp = ""
    start = time.time()
    while time.time() - start < timeout:
        if ser.in_waiting:
            data = ser.read(ser.in_waiting).decode("utf-8", errors="replace")
            resp += data
            if expect and (expect in resp or "ERROR" in resp):
                break
        time.sleep(0.01)

    resp_clean = resp.strip()
    print(f"<<< {resp_clean}")
    return resp


def wait_cereg(ser, retries=30, interval=1.0):
    """等待网络注册 (AT+CEREG?)"""
    for i in range(retries):
        resp = send_at(ser, "AT+CEREG?", timeout=2.0)
        if "+CEREG:" in resp:
            if ",1" in resp or ",5" in resp:
                print(f"网络已注册 (第{i+1}次查询)")
                return True
        print(f"等待网络注册... ({i+1}/{retries})")
        time.sleep(interval)
    print("网络注册超时")
    return False


def modem_init(ser):
    """modemInit() 完整流程"""

    # 1. AT - 测试通信
    while True:
        resp = send_at(ser, "AT", timeout=1.0)
        if "OK" in resp:
            print("模组AT响应正常")
            break
        print("AT未响应，重试...")
        time.sleep(0.5)

    # 2. ATI - 查询模组型号
    resp = send_at(ser, "ATI", timeout=2.0)
    need_set_cgact = True
    if "OK" in resp:
        lines = [l.strip() for l in resp.split("\n") if l.strip() and l.strip() not in ("ATI", "OK")]
        model = lines[1] if len(lines) > 1 else "未知"
        print(f"模组型号: {model}")
        if model == "ML307Y":
            need_set_cgact = False
            print("该型号(ATI有bug)跳过CGACT配置")

    # 3. AT+CGACT=0,1 - 禁用数据连接
    if need_set_cgact:
        while True:
            resp = send_at(ser, "AT+CGACT=0,1", timeout=5.0)
            if "OK" in resp:
                print("已禁用数据连接(AT+CGACT=0,1)")
                break
            print("设置CGACT失败，重试...")
            time.sleep(0.5)
    else:
        print("跳过 AT+CGACT=0,1")

    # 4. AT+CNMI=2,2,0,0,0 - 配置新消息通知
    while True:
        resp = send_at(ser, "AT+CNMI=2,2,0,0,0", timeout=1.0)
        if "OK" in resp:
            print("CNMI参数设置完成")
            break
        print("设置CNMI失败，重试...")
        time.sleep(0.5)

    # 5. AT+CMGF=0 - 设置PDU模式
    while True:
        resp = send_at(ser, "AT+CMGF=0", timeout=1.0)
        if "OK" in resp:
            print("PDU模式设置完成")
            break
        print("设置PDU模式失败，重试...")
        time.sleep(0.5)

    # 6. AT+CEREG? - 等待网络注册
    if wait_cereg(ser):
        print("网络已注册, modemReady=true")
        return True
    else:
        print("网络注册超时, modemReady=false")
        return False



def main():
    parser = argparse.ArgumentParser(description="模拟短信转发器开机AT命令流程")
    parser.add_argument("-p", "--port", default="/dev/serial0", help="串口设备路径 (默认: /dev/ttyUSB0)")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="波特率 (默认: 115200)")
    parser.add_argument("--skip-esim", action="store_true", help="跳过eSIM初始化")
    args = parser.parse_args()

    print(f"连接串口: {args.port} @ {args.baud}")
    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
        time.sleep(0.1)
    except serial.SerialException as e:
        print(f"串口打开失败: {e}")
        sys.exit(1)

    print("=" * 50)
    print("开始: 从开机到禁止使用网络的AT命令流程")
    print("=" * 50)

    # 模组初始化 (含禁用网络)
    modem_ok = modem_init(ser)


    print("\n" + "=" * 50)
    print(f"流程完成. modemReady={modem_ok}")
    print("=" * 50)

    ser.close()


if __name__ == "__main__":
    main()
