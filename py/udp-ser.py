# udp_server_enhanced.py
import socket
import threading
import time
import json
from datetime import datetime

class EnhancedUDPServer:
    def __init__(self, host='0.0.0.0', port=12300):
        self.host = host
        self.port = port
        self.socket = None
        self.running = False
        self.clients = {}  # 记录客户端信息
        self.message_queue = []

    def start(self):
        """启动增强版UDP服务器"""
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.socket.bind((self.host, self.port))
        self.running = True

        print(f"增强UDP服务器启动 - {self.host}:{self.port}")
        print(f"功能: 多客户端管理、消息转发、日志记录")

        # 启动接收线程
        receive_thread = threading.Thread(target=self.receive_loop)
        receive_thread.daemon = True
        receive_thread.start()

        # 启动命令处理线程
        cmd_thread = threading.Thread(target=self.command_loop)
        cmd_thread.daemon = True
        cmd_thread.start()

        # 启动心跳检测线程
        heartbeat_thread = threading.Thread(target=self.heartbeat_check)
        heartbeat_thread.daemon = True
        heartbeat_thread.start()

    def receive_loop(self):
        """接收数据循环"""
        while self.running:
            try:
                data, addr = self.socket.recvfrom(4096)
                self.handle_message(data, addr)
            except Exception as e:
                if self.running:
                    print(f"接收错误: {e}")

    def handle_message(self, data, addr):
        """处理接收到的消息"""
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]

        try:
            # 尝试解码
            message = data.decode('utf-8', errors='ignore')
        except:
            message = data.hex()

        # 更新客户端信息
        if addr not in self.clients:
            self.clients[addr] = {
                'first_seen': timestamp,
                'last_seen': timestamp,
                'messages': 0,
                'last_message': ''
            }

        self.clients[addr]['last_seen'] = timestamp
        self.clients[addr]['messages'] += 1
        self.clients[addr]['last_message'] = message[:100]  # 只记录前100字符

        # 特殊消息处理
        if message.startswith('HELLO:'):
            client_name = message[6:]
            self.clients[addr]['name'] = client_name
            print(f"\n[{timestamp}] 新客户端连接: {client_name} from {addr}")
            response = f"WELCOME:{client_name}"
            self.socket.sendto(response.encode('utf-8'), addr)

        elif message == 'PING':
            response = f"PONG:{timestamp}"
            self.socket.sendto(response.encode('utf-8'), addr)

        elif message == 'BYE':
            client_name = self.clients[addr].get('name', str(addr))
            print(f"\n[{timestamp}] 客户端断开: {client_name}")
            if addr in self.clients:
                del self.clients[addr]

        elif message.startswith('FILE:'):
            parts = message[5:].split(':')
            if len(parts) >= 2:
                filename, filesize = parts[0], parts[1]
                print(f"\n[{timestamp}] 接收文件: {filename} ({filesize} bytes) from {addr}")

        else:
            # 普通消息
            client_name = self.clients[addr].get('name', str(addr))
            print(f"\n[{timestamp}] 来自 {client_name} ({addr}):")
            print(f"  内容: {message}")

            # 自动回复
            response = f"ACK:{timestamp}"
            self.socket.sendto(response.encode('utf-8'), addr)

    def command_loop(self):
        """命令处理循环"""
        print("\n服务器命令:")
        print("  list    - 列出所有客户端")
        print("  send    - 发送消息到指定客户端")
        print("  stats   - 显示统计信息")
        print("  quit    - 退出服务器")

        while self.running:
            try:
                cmd = input("\n> ").strip().lower()

                if cmd == 'list':
                    self.list_clients()
                elif cmd == 'send':
                    self.send_to_client()
                elif cmd == 'stats':
                    self.show_stats()
                elif cmd == 'quit':
                    self.stop()
                    break

            except KeyboardInterrupt:
                self.stop()
                break
            except Exception as e:
                print(f"命令错误: {e}")

    def list_clients(self):
        """列出所有客户端"""
        if not self.clients:
            print("当前无连接客户端")
            return

        print("\n当前连接客户端:")
        print("-" * 80)
        for addr, info in self.clients.items():
            name = info.get('name', 'Unknown')
            print(f"  客户端: {name}")
            print(f"  地址: {addr[0]}:{addr[1]}")
            print(f"  首次连接: {info['first_seen']}")
            print(f"  最后活跃: {info['last_seen']}")
            print(f"  消息数: {info['messages']}")
            print("-" * 80)

    def send_to_client(self):
        """发送消息到指定客户端"""
        if not self.clients:
            print("无可用客户端")
            return

        self.list_clients()

        # 选择客户端
        client_list = list(self.clients.keys())
        if len(client_list) == 1:
            target = client_list[0]
        else:
            try:
                idx = int(input(f"选择客户端 (0-{len(client_list)-1}): "))
                target = client_list[idx]
            except:
                print("无效选择")
                return

        # 输入消息
        message = input("输入消息: ").strip()
        if message:
            self.socket.sendto(message.encode('utf-8'), target)
            print(f"已发送到 {target}")

    def show_stats(self):
        """显示统计信息"""
        total_messages = sum(c['messages'] for c in self.clients.values())
        print("\n服务器统计:")
        print(f"  运行时间: 待实现")
        print(f"  活跃客户端: {len(self.clients)}")
        print(f"  总消息数: {total_messages}")

    def heartbeat_check(self):
        """心跳检测"""
        while self.running:
            time.sleep(30)
            current_time = time.time()

            # 检查超时客户端
            timeout_clients = []
            for addr, info in self.clients.items():
                last_seen = datetime.strptime(info['last_seen'], "%Y-%m-%d %H:%M:%S.%f")
                if (datetime.now() - last_seen).seconds > 300:  # 5分钟超时
                    timeout_clients.append(addr)

            for addr in timeout_clients:
                client_name = self.clients[addr].get('name', str(addr))
                print(f"\n客户端超时: {client_name}")
                del self.clients[addr]

    def stop(self):
        """停止服务器"""
        print("\n正在停止服务器...")
        self.running = False

        # 通知所有客户端
        for addr in self.clients:
            self.socket.sendto(b"SERVER_SHUTDOWN", addr)

        if self.socket:
            self.socket.close()
        print("服务器已停止")

if __name__ == "__main__":
    import socket as sock

    # 获取本机IP
    hostname = sock.gethostname()
    local_ip = sock.gethostbyname(hostname)
    print(f"本机IP地址: {local_ip}")

    server = EnhancedUDPServer(port=12300)
    try:
        server.start()

        # 保持主线程运行
        while server.running:
            time.sleep(1)

    except KeyboardInterrupt:
        server.stop()
