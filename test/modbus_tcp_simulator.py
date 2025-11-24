#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Modbus TCP传感器模拟器
模拟4个传感器：上压力、下压力、扭矩、位移
采样率：10Hz
数据格式：IEEE754 float32
"""

import socket
import struct
import time
import threading
import argparse
import math


class ModbusTCPSimulator:
    """Modbus TCP服务器模拟器"""

    # Modbus功能码
    FUNC_READ_HOLDING_REGISTERS = 0x03
    FUNC_READ_INPUT_REGISTERS = 0x04

    # 寄存器地址映射（每个浮点数占2个寄存器）
    REG_FORCE_UPPER = 0  # 上压力传感器 (0-1寄存器)
    REG_FORCE_LOWER = 2  # 下压力传感器 (2-3寄存器)
    REG_TORQUE = 4       # 扭矩传感器 (4-5寄存器)
    REG_POSITION = 6     # 位移传感器 (6-7寄存器)

    def __init__(self, host='0.0.0.0', port=502):
        self.host = host
        self.port = port
        self.running = False
        self.server_socket = None

        # 模拟传感器数据（单位：N, N, N·m, mm）
        self.force_upper = 0.0
        self.force_lower = 0.0
        self.torque = 0.0
        self.position = 0.0

        # 模拟参数
        self.time_start = time.time()

    def start(self):
        """启动TCP服务器"""
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind((self.host, self.port))
        self.server_socket.listen(5)
        self.running = True

        print(f"Modbus TCP模拟器启动在 {self.host}:{self.port}")
        print("传感器：上压力、下压力、扭矩、位移")

        # 启动数据更新线程
        update_thread = threading.Thread(target=self._update_sensors, daemon=True)
        update_thread.start()

        # 处理客户端请求
        self._handle_clients()

    def stop(self):
        """停止服务器"""
        self.running = False
        if self.server_socket:
            self.server_socket.close()
        print("Modbus TCP模拟器已停止")

    def _update_sensors(self):
        """更新模拟传感器数据"""
        while self.running:
            t = time.time() - self.time_start

            # 模拟周期性变化的数据
            # 上压力：1000-3000N的正弦变化
            self.force_upper = 2000 + 1000 * math.sin(0.5 * t)

            # 下压力：500-1500N的正弦变化
            self.force_lower = 1000 + 500 * math.sin(0.8 * t + 1)

            # 扭矩：50-150N·m的正弦变化
            self.torque = 100 + 50 * math.sin(0.3 * t + 2)

            # 位移：0-100mm的线性增长（循环）
            self.position = (t * 10) % 100

            time.sleep(0.1)  # 10Hz更新

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
        """处理单个客户端的Modbus请求"""
        try:
            while self.running:
                # 接收Modbus TCP请求（MBAP Header + PDU）
                request = client_socket.recv(1024)
                if not request:
                    break

                # 解析MBAP Header (7 bytes)
                if len(request) < 8:
                    continue

                trans_id = struct.unpack('>H', request[0:2])[0]
                proto_id = struct.unpack('>H', request[2:4])[0]
                length = struct.unpack('>H', request[4:6])[0]
                unit_id = request[6]

                # 解析PDU
                func_code = request[7]
                start_reg = struct.unpack('>H', request[8:10])[0]
                num_regs = struct.unpack('>H', request[10:12])[0]

                # 处理读寄存器请求
                if func_code in [self.FUNC_READ_HOLDING_REGISTERS, self.FUNC_READ_INPUT_REGISTERS]:
                    response = self._read_registers(trans_id, unit_id, func_code, start_reg, num_regs)
                    client_socket.sendall(response)
                else:
                    print(f"不支持的功能码: 0x{func_code:02X}")

        except Exception as e:
            print(f"客户端处理错误: {e}")
        finally:
            client_socket.close()
            print("客户端断开")

    def _read_registers(self, trans_id, unit_id, func_code, start_reg, num_regs):
        """
        读取寄存器并返回Modbus响应
        IEEE754 float占用2个寄存器（高位在前）
        """
        # 准备浮点数数据（4个传感器）
        values = [
            self.force_upper,
            self.force_lower,
            self.torque,
            self.position
        ]

        # 转换为寄存器字节
        reg_bytes = b''
        for val in values:
            # float32 -> 4字节 -> 2个寄存器（大端）
            float_bytes = struct.pack('>f', val)
            reg_bytes += float_bytes

        # 提取请求的寄存器
        start_byte = start_reg * 2
        num_bytes = num_regs * 2
        data_bytes = reg_bytes[start_byte:start_byte + num_bytes]

        # 构造响应PDU
        pdu = struct.pack('BB', func_code, len(data_bytes)) + data_bytes

        # 构造MBAP Header
        response_length = len(pdu) + 1  # PDU + unit_id
        mbap = struct.pack('>HHHB', trans_id, 0, response_length, unit_id)

        return mbap + pdu

    def print_status(self):
        """打印当前传感器状态"""
        print(f"\r上压力: {self.force_upper:7.2f}N | "
              f"下压力: {self.force_lower:7.2f}N | "
              f"扭矩: {self.torque:6.2f}N·m | "
              f"位移: {self.position:5.2f}mm", end='')


def main():
    parser = argparse.ArgumentParser(description='Modbus TCP传感器模拟器')
    parser.add_argument('--host', default='0.0.0.0', help='监听地址')
    parser.add_argument('--port', type=int, default=502, help='监听端口')

    args = parser.parse_args()

    simulator = ModbusTCPSimulator(host=args.host, port=args.port)

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
