#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
VK701振动采集卡模拟器
模拟3通道振动传感器数据采集
采样率：5000Hz
数据格式：float32数组
"""

import socket
import struct
import time
import numpy as np
import threading
import argparse


class VK701Simulator:
    """VK701振动采集卡TCP服务器模拟器"""

    def __init__(self, host='192.168.1.10', port=8234, channels=3, sample_rate=5000):
        self.host = host
        self.port = port
        self.channels = channels
        self.sample_rate = sample_rate
        self.running = False
        self.server_socket = None
        self.clients = []

        # 模拟振动参数
        self.frequencies = [50, 100, 150]  # Hz - 每个通道的主频率
        self.amplitudes = [1.0, 0.8, 1.2]  # 振幅
        self.phase = 0.0

    def start(self):
        """启动TCP服务器"""
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind((self.host, self.port))
        self.server_socket.listen(5)
        self.running = True

        print(f"VK701模拟器启动在 {self.host}:{self.port}")
        print(f"通道数: {self.channels}, 采样率: {self.sample_rate}Hz")

        # 启动接受连接线程
        accept_thread = threading.Thread(target=self._accept_clients, daemon=True)
        accept_thread.start()

        # 启动数据发送线程
        send_thread = threading.Thread(target=self._send_data, daemon=True)
        send_thread.start()

    def stop(self):
        """停止服务器"""
        self.running = False
        for client in self.clients:
            try:
                client.close()
            except:
                pass
        if self.server_socket:
            self.server_socket.close()
        print("VK701模拟器已停止")

    def _accept_clients(self):
        """接受客户端连接"""
        while self.running:
            try:
                client_socket, address = self.server_socket.accept()
                print(f"新客户端连接: {address}")
                self.clients.append(client_socket)
            except:
                break

    def _generate_vibration_data(self, num_samples):
        """
        生成模拟振动数据
        返回：shape=(channels, num_samples)的numpy数组
        """
        # 时间轴
        t = np.arange(num_samples) / self.sample_rate + self.phase

        # 为每个通道生成数据
        data = np.zeros((self.channels, num_samples), dtype=np.float32)
        for ch in range(self.channels):
            # 主频率分量
            signal = self.amplitudes[ch] * np.sin(2 * np.pi * self.frequencies[ch] * t)

            # 添加一些谐波
            signal += 0.3 * np.sin(2 * np.pi * self.frequencies[ch] * 2 * t)
            signal += 0.1 * np.sin(2 * np.pi * self.frequencies[ch] * 3 * t)

            # 添加随机噪声
            signal += 0.05 * np.random.randn(num_samples)

            data[ch, :] = signal

        # 更新相位以保持连续性
        self.phase = (self.phase + num_samples / self.sample_rate) % 1.0

        return data

    def _send_data(self):
        """持续发送数据给所有连接的客户端"""
        block_size = 1000  # 每次发送1000个采样点
        interval = block_size / self.sample_rate  # 发送间隔

        while self.running:
            if not self.clients:
                time.sleep(0.1)
                continue

            # 生成数据块
            data = self._generate_vibration_data(block_size)

            # 构造数据包：
            # Header: [MAGIC(4bytes)] [CHANNELS(4bytes)] [SAMPLES(4bytes)] [TIMESTAMP(8bytes)]
            # Data: [CH0_DATA] [CH1_DATA] [CH2_DATA] ... (每个float32)

            magic = 0x564B3730  # "VK70"
            timestamp_us = int(time.time() * 1e6)

            header = struct.pack('<IIIQ', magic, self.channels, block_size, timestamp_us)

            # 将数据按通道顺序展开（channel-first）
            data_bytes = data.tobytes()

            packet = header + data_bytes

            # 发送给所有客户端
            disconnected = []
            for client in self.clients:
                try:
                    client.sendall(packet)
                except:
                    disconnected.append(client)

            # 移除断开的客户端
            for client in disconnected:
                print("客户端断开连接")
                self.clients.remove(client)
                try:
                    client.close()
                except:
                    pass

            # 等待下一个数据块
            time.sleep(interval)


def main():
    parser = argparse.ArgumentParser(description='VK701振动采集卡模拟器')
    parser.add_argument('--host', default='0.0.0.0', help='监听地址')
    parser.add_argument('--port', type=int, default=8234, help='监听端口')
    parser.add_argument('--channels', type=int, default=3, help='通道数')
    parser.add_argument('--rate', type=int, default=5000, help='采样率(Hz)')

    args = parser.parse_args()

    simulator = VK701Simulator(
        host=args.host,
        port=args.port,
        channels=args.channels,
        sample_rate=args.rate
    )

    try:
        simulator.start()
        print("按Ctrl+C停止服务器...")
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n正在停止服务器...")
        simulator.stop()


if __name__ == '__main__':
    main()
