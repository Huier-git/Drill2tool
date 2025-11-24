#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ZMotion运动控制器模拟器
模拟多个电机的运动参数
采样率：100Hz
参数：位置(DPOS)、速度(SPEED)、扭矩(TORQUE)、电流(CURRENT)
"""

import socket
import struct
import time
import threading
import argparse
import math
import json


class ZMotionSimulator:
    """ZMotion运动控制器TCP服务器模拟器"""

    def __init__(self, host='0.0.0.0', port=8001, num_motors=4):
        self.host = host
        self.port = port
        self.num_motors = num_motors
        self.running = False
        self.server_socket = None

        # 每个电机的参数
        self.motors = []
        for i in range(num_motors):
            self.motors.append({
                'id': i,
                'position': 0.0,       # 位置 (脉冲或度)
                'speed': 0.0,          # 速度 (units/s)
                'torque': 0.0,         # 扭矩 (N·m)
                'current': 0.0,        # 电流 (A)
                'enabled': True,
                'moving': False
            })

        self.time_start = time.time()

    def start(self):
        """启动TCP服务器"""
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind((self.host, self.port))
        self.server_socket.listen(5)
        self.running = True

        print(f"ZMotion模拟器启动在 {self.host}:{self.port}")
        print(f"电机数量: {self.num_motors}")

        # 启动数据更新线程
        update_thread = threading.Thread(target=self._update_motors, daemon=True)
        update_thread.start()

        # 处理客户端请求
        self._handle_clients()

    def stop(self):
        """停止服务器"""
        self.running = False
        if self.server_socket:
            self.server_socket.close()
        print("ZMotion模拟器已停止")

    def _update_motors(self):
        """更新电机参数"""
        while self.running:
            t = time.time() - self.time_start

            for i, motor in enumerate(self.motors):
                # 模拟不同的运动模式
                phase = i * math.pi / 2  # 不同电机相位差

                # 位置：0-360度的周期运动
                motor['position'] = 180 + 180 * math.sin(0.5 * t + phase)

                # 速度：-100 ~ 100 units/s
                motor['speed'] = 100 * math.cos(0.5 * t + phase) * 0.5

                # 扭矩：0-10N·m的波动
                motor['torque'] = 5 + 5 * math.sin(0.3 * t + phase) + \
                                 0.5 * math.sin(2.0 * t)  # 加上高频分量

                # 电流：与扭矩成正比，1-5A
                motor['current'] = 2 + motor['torque'] / 3.0 + \
                                  0.2 * (hash(str(i)) % 100) / 100  # 加上随机偏移

                # 运动状态
                motor['moving'] = abs(motor['speed']) > 1.0

            time.sleep(0.01)  # 100Hz更新

    def _handle_clients(self):
        """处理客户端连接"""
        while self.running:
            try:
                client_socket, address = self.server_socket.accept()
                print(f"新客户端连接: {address}")

                # 为每个客户端创建处理线程
                client_thread = threading.Thread(
                    target=self._handle_client,
                    args=(client_socket,),
                    daemon=True
                )
                client_thread.start()

            except Exception as e:
                if self.running:
                    print(f"接受连接错误: {e}")
                break

    def _handle_client(self, client_socket):
        """处理单个客户端的命令请求"""
        try:
            while self.running:
                # 接收命令
                request = client_socket.recv(1024)
                if not request:
                    break

                try:
                    # 简单的文本协议
                    command = request.decode('utf-8').strip()

                    # 解析命令
                    response = self._process_command(command)

                    # 发送响应
                    client_socket.sendall((response + '\n').encode('utf-8'))

                except Exception as e:
                    error_msg = f"ERROR: {str(e)}"
                    client_socket.sendall(error_msg.encode('utf-8'))

        except Exception as e:
            print(f"客户端处理错误: {e}")
        finally:
            client_socket.close()
            print("客户端断开")

    def _process_command(self, command):
        """
        处理ZMotion命令
        支持的命令格式：
        - GET_DPOS(axis)  - 获取位置
        - GET_SPEED(axis) - 获取速度
        - GET_TORQUE(axis) - 获取扭矩
        - GET_CURRENT(axis) - 获取电流
        - GET_ALL(axis) - 获取所有参数
        - GET_ALL_MOTORS - 获取所有电机所有参数（JSON格式）
        """
        parts = command.split('(')
        if len(parts) != 2:
            return "ERROR: Invalid command format"

        cmd = parts[0].upper()
        args = parts[1].rstrip(')').split(',')

        if cmd == 'GET_ALL_MOTORS':
            # 返回所有电机的JSON数据
            return json.dumps({'motors': self.motors})

        if len(args) < 1:
            return "ERROR: Missing axis parameter"

        try:
            axis = int(args[0])
            if axis < 0 or axis >= self.num_motors:
                return f"ERROR: Invalid axis {axis}"
        except ValueError:
            return "ERROR: Invalid axis number"

        motor = self.motors[axis]

        if cmd == 'GET_DPOS':
            return f"OK: {motor['position']:.4f}"
        elif cmd == 'GET_SPEED':
            return f"OK: {motor['speed']:.4f}"
        elif cmd == 'GET_TORQUE':
            return f"OK: {motor['torque']:.4f}"
        elif cmd == 'GET_CURRENT':
            return f"OK: {motor['current']:.4f}"
        elif cmd == 'GET_ALL':
            return json.dumps({
                'axis': axis,
                'position': motor['position'],
                'speed': motor['speed'],
                'torque': motor['torque'],
                'current': motor['current'],
                'enabled': motor['enabled'],
                'moving': motor['moving']
            })
        else:
            return f"ERROR: Unknown command {cmd}"

    def print_status(self):
        """打印所有电机状态"""
        print(f"\r时间: {time.time() - self.time_start:.1f}s | ", end='')
        for i, motor in enumerate(self.motors):
            print(f"M{i}: Pos={motor['position']:6.1f}° "
                  f"Spd={motor['speed']:5.1f} "
                  f"Trq={motor['torque']:4.1f}N·m "
                  f"Cur={motor['current']:4.1f}A | ", end='')
        print(end='')


def main():
    parser = argparse.ArgumentParser(description='ZMotion运动控制器模拟器')
    parser.add_argument('--host', default='0.0.0.0', help='监听地址')
    parser.add_argument('--port', type=int, default=8001, help='监听端口')
    parser.add_argument('--motors', type=int, default=4, help='电机数量')

    args = parser.parse_args()

    simulator = ZMotionSimulator(
        host=args.host,
        port=args.port,
        num_motors=args.motors
    )

    try:
        # 启动状态显示线程
        def print_status_loop():
            while simulator.running:
                simulator.print_status()
                time.sleep(0.5)

        status_thread = threading.Thread(target=print_status_loop, daemon=True)
        status_thread.start()

        print("按Ctrl+C停止服务器...")
        simulator.start()

    except KeyboardInterrupt:
        print("\n\n正在停止服务器...")
        simulator.stop()


if __name__ == '__main__':
    main()
