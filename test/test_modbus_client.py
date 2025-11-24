#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
测试 Modbus TCP 连接
"""

from pymodbus.client import ModbusTcpClient
import time

def test_modbus_connection():
    print("=" * 50)
    print("测试 Modbus TCP 连接")
    print("=" * 50)
    
    # 连接到模拟器
    print("\n1. 连接到 127.0.0.1:502...")
    client = ModbusTcpClient('127.0.0.1', port=502, timeout=3)
    
    if not client.connect():
        print("❌ 连接失败！请确保模拟器正在运行。")
        return
    
    print("✅ 连接成功！")
    
    # 读取寄存器
    print("\n2. 读取传感器数据...")
    try:
        # 读取 8 个寄存器（4 个 float32 值）
        result = client.read_holding_registers(address=0, count=8, unit=1)
        
        if result.isError():
            print(f"❌ 读取失败：{result}")
        else:
            print("✅ 读取成功！")
            print(f"   原始寄存器值: {result.registers}")
            
            # 解析 float32 值
            import struct
            values = []
            for i in range(0, 8, 2):
                high = result.registers[i]
                low = result.registers[i+1]
                bytes_data = struct.pack('>HH', high, low)
                float_val = struct.unpack('>f', bytes_data)[0]
                values.append(float_val)
            
            print(f"\n   解析后的传感器值:")
            print(f"   - 上压力: {values[0]:.2f} N")
            print(f"   - 下压力: {values[1]:.2f} N")
            print(f"   - 扭矩:   {values[2]:.2f} N·m")
            print(f"   - 位移:   {values[3]:.2f} mm")
            
    except Exception as e:
        print(f"❌ 异常：{e}")
    
    # 关闭连接
    client.close()
    print("\n3. 连接已关闭")
    print("\n" + "=" * 50)
    print("测试完成！")
    print("=" * 50)

if __name__ == '__main__':
    test_modbus_connection()
