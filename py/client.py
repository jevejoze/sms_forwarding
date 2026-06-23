# udp_client.py
import socket
import threading
import time
import sys

class UDPClient:
    def __init__(self, server_ip, server_port=12300):
        self.server_ip = server_ip
        self.server_port = server_port
        self.socket = None
        self.running = False
        self.client_id = f"Client-{int(time.time())}"

    def connect(self):
        """创建UDP socket"""
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # 绑定本地随机端口
        self.socket.bind(('0.0.0.0', 0))
        local_port = self.socket.getsockname()[1]
        self.running = True
        print(f"UDP客户端启动")
        print(f"本地端口: {local_port}")
        print(f"目标服务器: {self.server_ip}:{self.server_port}")
        return True

    def send_message(self, message):
        """发送消息到服务器"""
        try:
            if isinstance(message, str):
                data = message.encode('utf-8')
            else:
                data = message

            self.socket.sendto(data, (self.server_ip, self.server_port))
            print(f"发送: {message}")

            # 等待响应
            self.socket.settimeout(3)
            try:
                response, addr = self.socket.recvfrom(1024)
                print(f"收到响应: {response.decode('utf-8')}")
                return response.decode('utf-8')
            except socket.timeout:
                print("等待响应超时")
                return None

        except Exception as e:
            print(f"发送失败: {e}")
            return None

    def receive_messages(self, callback=None):
        """持续接收消息的线程"""
        self.socket.settimeout(1)
        while self.running:
            try:
                data, addr = self.socket.recvfrom(1024)
                message = data.decode('utf-8')
                print(f"\n收到来自 {addr} 的消息: {message}")
                if callback:
                    callback(message, addr)
            except socket.timeout:
                continue
            except Exception as e:
                if self.running:
                    print(f"接收错误: {e}")

    def start_interactive(self):
        """交互式客户端"""
        print("\n" + "="*50)
        print("UDP客户端 - 交互模式")
        print("命令:")
        print("  /quit   - 退出")
        print("  /hex    - 发送十六进制数据")
        print("  /ping   - 发送心跳包")
        print("  /file   - 发送文件内容")
        print("  直接输入文本发送消息")
        print("="*50)

        # 启动接收线程
        receive_thread = threading.Thread(target=self.receive_messages)
        receive_thread.daemon = True
        receive_thread.start()

        # 发送心跳
        self.send_message(f"HELLO:{self.client_id}")

        while self.running:
            try:
                user_input = input("\n请输入消息: ").strip()

                if not user_input:
                    continue

                if user_input == '/quit':
                    break
                elif user_input == '/ping':
                    self.send_message("PING")
                elif user_input.startswith('/hex '):
                    hex_data = user_input[5:].replace(' ', '')
                    try:
                        binary_data = bytes.fromhex(hex_data)
                        self.send_message(binary_data)
                    except ValueError:
                        print("无效的十六进制字符串")
                elif user_input.startswith('/file '):
                    filepath = user_input[6:].strip()
                    self.send_file(filepath)
                else:
                    self.send_message(user_input)

            except KeyboardInterrupt:
                break

        self.close()

    def send_file(self, filepath, chunk_size=1024):
        """发送文件内容"""
        try:
            with open(filepath, 'rb') as f:
                file_data = f.read()

            # 发送文件信息
            import os
            filename = os.path.basename(filepath)
            file_size = len(file_data)

            info_msg = f"FILE:{filename}:{file_size}"
            self.send_message(info_msg)

            # 分块发送
            chunks = [file_data[i:i+chunk_size] for i in range(0, file_size, chunk_size)]
            for i, chunk in enumerate(chunks):
                chunk_msg = f"CHUNK:{i}:".encode('utf-8') + chunk
                self.socket.sendto(chunk_msg, (self.server_ip, self.server_port))
                time.sleep(0.01)  # 避免发送过快

            # 发送结束标记
            self.send_message(f"FILE_END:{filename}")
            print(f"文件发送完成: {filename} ({file_size} bytes)")

        except FileNotFoundError:
            print(f"文件不存在: {filepath}")
        except Exception as e:
            print(f"发送文件失败: {e}")

    def close(self):
        """关闭客户端"""
        self.running = False
        if self.socket:
            self.send_message("BYE")
            self.socket.close()
        print("客户端已关闭")

def main():
    """主函数"""
    print("UDP客户端")
    print("-" * 50)

    # 获取服务器信息
    server_ip = input("请输入服务器IP (默认: 127.0.0.1): ").strip()
    if not server_ip:
        server_ip = 'orasing.arick.top'

    server_port = input("请输入服务器端口 (默认: 12300): ").strip()
    server_port = int(server_port) if server_port else 12300

    # 创建客户端
    client = UDPClient(server_ip, server_port)

    try:
        if client.connect():
            client.start_interactive()
    except KeyboardInterrupt:
        print("\n程序中断")
    except Exception as e:
        print(f"错误: {e}")
    finally:
        client.close()

if __name__ == "__main__":
    main()
