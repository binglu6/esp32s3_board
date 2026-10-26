# 读取 COM3 启动日志：复位芯片后采集指定秒数的串口输出
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM3"
SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 12.0

ser = serial.Serial(PORT, 115200, timeout=0.2)
# 通过 RTS 复位芯片（ESP32-S3 开发板常见接法）
ser.setDTR(False)
ser.setRTS(True)
time.sleep(0.1)
ser.setRTS(False)
time.sleep(0.1)

deadline = time.time() + SECONDS
data = bytearray()
while time.time() < deadline:
    chunk = ser.read(8192)
    if chunk:
        data.extend(chunk)
ser.close()

sys.stdout.buffer.write(bytes(data))
