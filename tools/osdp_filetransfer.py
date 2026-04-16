import argparse
import struct
import time
from pathlib import Path

import serial
from serial.tools import list_ports

OSDP_SOM = 0x53

CMD_FILETRANSFER = 0x7C
REPLY_FTSTAT = 0x7A

OSDP_INIT_CRC16 = 0x1D0F


CRC_TABLE = [
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0,
]

CH340_VID_PID = {(0x1A86, 0x7523), (0x1A86, 0x5523)}
KNOWN_USB_UART_VID_PID = {
    (0x1A86, 0x7523), (0x1A86, 0x5523), (0x1A86, 0x55D4),
    (0x10C4, 0xEA60),
    (0x0403, 0x6001), (0x0403, 0x6010),
    (0x067B, 0x2303),
    (0x4348, 0x5523),
}
USB_UART_HINTS = (
    "ch340", "ch341", "ch910", "cp210", "ftdi", "ft232",
    "pl2303", "usb serial", "usb-serial", "uart", "cdc",
)


def ccitt_crc16(data: bytes, init: int = OSDP_INIT_CRC16) -> int:
    crc = init
    for b in data:
        crc = ((crc << 8) & 0xFFFF) ^ CRC_TABLE[((crc >> 8) ^ b) & 0xFF]
    return crc


def crc32_bl(data: bytes) -> int:
    """CRC32 как в bootloader/src/bl_crc32.c"""
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= (b << 24) & 0xFFFFFFFF
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc ^ 0xFFFFFFFF


def _port_sort_key(port_info):
    dev = (port_info.device or "").upper()
    if dev.startswith("COM"):
        try:
            return (0, int(dev[3:]))
        except ValueError:
            return (1, dev)
    return (1, dev)


def _port_score(port_info) -> int:
    score = 0
    if (port_info.vid, port_info.pid) in CH340_VID_PID:
        score += 200
    if (port_info.vid, port_info.pid) in KNOWN_USB_UART_VID_PID:
        score += 150
    if port_info.vid is not None and port_info.pid is not None:
        score += 50

    text = " ".join(
        [
            port_info.description or "",
            port_info.manufacturer or "",
            port_info.product or "",
            port_info.hwid or "",
            port_info.interface or "",
        ]
    ).lower()
    if any(h in text for h in USB_UART_HINTS):
        score += 80
    if "bluetooth" in text:
        score -= 100
    if "debug" in text or "jtag" in text:
        score -= 40
    return score


def _port_line(port_info) -> str:
    return (
        f"{port_info.device}: "
        f"{port_info.description or 'n/a'} | "
        f"VID:PID=0x{(port_info.vid or 0):04X}:0x{(port_info.pid or 0):04X} | "
        f"score={_port_score(port_info)}"
    )


def list_serial_ports() -> None:
    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return
    ports.sort(key=_port_sort_key)
    print("Available serial ports:")
    for p in ports:
        print(" - " + _port_line(p))


def detect_serial_port(port_filter: str = "") -> str:
    ports = list(list_ports.comports())
    if not ports:
        raise RuntimeError("No serial ports found")

    if port_filter:
        import re
        rx = re.compile(port_filter, re.IGNORECASE)
        filtered = []
        for p in ports:
            text = " ".join([p.device or "", p.description or "", p.manufacturer or "", p.product or "", p.hwid or ""])
            if rx.search(text):
                filtered.append(p)
        ports = filtered
        if not ports:
            raise RuntimeError(
                f"No serial ports matched --port-filter '{port_filter}'. "
                "Use --list-ports to inspect available ports."
            )

    if len(ports) == 1:
        return ports[0].device

    ports.sort(key=lambda p: (-_port_score(p), _port_sort_key(p)))
    best = ports[0]
    second = ports[1]
    if _port_score(best) >= 150 and (_port_score(best) - _port_score(second)) >= 40:
        return best.device

    print("Multiple serial ports detected. Select port:")
    for i, p in enumerate(ports, start=1):
        print(f"  {i}) {_port_line(p)}")
    print("  0) Cancel")

    while True:
        choice = input("Enter number: ").strip()
        if choice == "0":
            raise RuntimeError("Canceled by user")
        if choice.isdigit():
            idx = int(choice)
            if 1 <= idx <= len(ports):
                return ports[idx - 1].device
        print("Invalid choice, try again.")


def build_osdp_frame(addr: int, seq: int, cmd: int, data: bytes) -> bytes:
    """Построить кадр OSDP с CRC (как в прошивке)."""
    ctrl = (seq & 0x03) | 0x04  # CRC-on, без secure channel, seq 1..3
    dlen = 8 + len(data)        # полная длина кадра
    header = bytes([
        OSDP_SOM,
        addr & 0x7F,
        dlen & 0xFF,
        (dlen >> 8) & 0xFF,
        ctrl,
        cmd & 0xFF,
    ])
    body = header + data
    crc = ccitt_crc16(body, OSDP_INIT_CRC16)
    return body + struct.pack('<H', crc)


def parse_ftstat(frame: bytes):
    """
    Разобрать REPLY_FTSTAT (0x7A) из полного кадра.
    Возвращает (status_detail, action, requested_delay_ms, update_max).
    """
    if len(frame) < 8:
        raise ValueError("frame too short")
    som, addr, len_l, len_h, ctrl, reply = frame[:6]
    if som != OSDP_SOM:
        raise ValueError("bad SOM")
    if reply != REPLY_FTSTAT:
        raise ValueError(f"unexpected reply 0x{reply:02X}")
    total_len = (len_h << 8) | len_l
    dlen = total_len - 8
    data = frame[6:6 + dlen]
    if len(data) < 7:
        raise ValueError("FTSTAT data too short")
    action = data[0]
    requested_delay = struct.unpack_from('<H', data, 1)[0]
    status_detail = struct.unpack_from('<h', data, 3)[0]
    update_max = struct.unpack_from('<H', data, 5)[0]
    return status_detail, action, requested_delay, update_max


def read_osdp_frame(ser: serial.Serial, timeout_s: float) -> bytes:
    deadline = time.monotonic() + timeout_s
    buf = bytearray()

    while time.monotonic() < deadline:
        b = ser.read(1)
        if not b:
            continue

        if not buf:
            if b[0] != OSDP_SOM:
                continue
            buf += b
            continue

        buf += b
        if len(buf) >= 4:
            frame_len = buf[2] | (buf[3] << 8)
            if frame_len < 8:
                buf.clear()
                continue
            if len(buf) >= frame_len:
                return bytes(buf[:frame_len])

    return b""


def send_file(port: str, baud: int, addr: int, image_path: Path,
              fragment_size: int = 64, port_filter: str = "", resp_timeout: float = 5.0):
    if port.lower() == "auto":
        port = detect_serial_port(port_filter)
        print(f"Auto-detected port: {port}")

    ser = serial.Serial(
        port,
        baudrate=baud,
        bytesize=8,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.2,
    )

    try:
        payload = image_path.read_bytes()
        if len(payload) == 0:
            raise RuntimeError("Input file is empty")

        # Формируем full image для OSDP transfer:
        # bl_app_header_t { image_size, crc32 } + payload
        image_size = len(payload)
        image_crc = crc32_bl(payload)
        header = struct.pack('<II', image_size, image_crc)
        image = header + payload
        ft_size_total = len(image)
        print(f"Payload: {image_size} bytes, CRC32(BL)=0x{image_crc:08X}")
        print(f"Sending {ft_size_total} bytes (header+payload) to PD addr {addr} via FILETRANSFER...")

        offset = 0
        seq = 1  # 1..3, как в прошивке
        while offset < ft_size_total:
            chunk = image[offset:offset + fragment_size]
            frag_len = len(chunk)

            # Собираем DATA для CMD_FILETRANSFER по Table 34
            data = bytearray()
            data.append(1)  # FtType = 1 (opaque прошивка)
            data += struct.pack('<I', ft_size_total)
            data += struct.pack('<I', offset)
            data += struct.pack('<H', frag_len)
            data += chunk

            frame = build_osdp_frame(addr, seq, CMD_FILETRANSFER, bytes(data))
            ser.write(frame)

            # На первом фрагменте PD может дольше отвечать (erase внешней flash).
            timeout_this = max(resp_timeout, 3.0) if offset == 0 else resp_timeout
            resp = read_osdp_frame(ser, timeout_this)
            if not resp:
                raise RuntimeError(f"No response from PD (timeout {timeout_this:.1f}s, offset={offset})")

            try:
                status_detail, action, delay_ms, update_max = parse_ftstat(resp)
            except ValueError as e:
                raise RuntimeError(f"Bad response: {e}, raw={resp.hex()}")

            if offset % 2000 == 0:
                print(f"Offset {offset}/{ft_size_total}")

            if status_detail < 0:
                raise RuntimeError(f"PD reported error detail={status_detail}")

            offset += frag_len

            if offset >= ft_size_total:
                # На последнем фрагменте ожидаем FileContentsProcessed (1),
                # но даже если деталь другая, файл уже записан.
                if status_detail != 1:
                    print("Warning: last fragment, but status_detail != 1 (FileContentsProcessed)")
                break

            # seq: 1 -> 2 -> 3 -> 1 ...
            seq = (seq % 3) + 1

            if delay_ms:
                time.sleep(delay_ms / 1000.0)

        print("Прошивка прошла успешно")
    finally:
        ser.close()


def main():
    parser = argparse.ArgumentParser(description="Send OSDP FILETRANSFER (0x7C) to PD")
    parser.add_argument("--port", default="auto", help="Serial port, e.g. COM5, or auto (default)")
    parser.add_argument("--port-filter", default="", help="Regex filter for auto mode, e.g. CH340|CP210")
    parser.add_argument("--list-ports", action="store_true", help="List serial ports and exit")
    parser.add_argument("--baud", type=int, default=115200, help="Baudrate (default 115200)")
    parser.add_argument("--addr", type=int, default=1, help="PD address (default 1)")
    parser.add_argument("--frag", type=int, default=64, help="Fragment size in bytes (default 64)")
    parser.add_argument("--resp-timeout", type=float, default=5.0, help="Response timeout in seconds (default 5.0)")
    parser.add_argument("image", type=Path, help="Application payload .bin (header will be generated)")
    args = parser.parse_args()

    if args.list_ports:
        list_serial_ports()
        return

    send_file(args.port, args.baud, args.addr, args.image, args.frag, args.port_filter, args.resp_timeout)


if __name__ == "__main__":
    main()

