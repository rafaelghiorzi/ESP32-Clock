#!/usr/bin/env python3
"""
Captura o log da porta serial USB do ESP32_Clock e salva num arquivo de
texto com timestamp em cada linha (além de continuar mostrando tudo na
tela, como o Serial Monitor normal).

Uso:
    python capture_usb_log.py COM5
    python capture_usb_log.py COM5 --baud 115200 --out meu_log.txt

Precisa da lib pyserial:
    pip install pyserial

Pra descobrir a porta COM certa no Windows: Gerenciador de Dispositivos >
Portas (COM e LPT), ou olhe qual porta a Arduino IDE mostra em Tools > Port
quando a placa está plugada.
"""
import argparse
import datetime
import sys

try:
    import serial
except ImportError:
    print("Falta instalar a biblioteca pyserial. Rode: pip install pyserial")
    sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port", help="Porta serial (ex: COM5 no Windows, /dev/ttyACM0 no Linux)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (padrao: 115200)")
    parser.add_argument("--out", default=None, help="Arquivo de saida (padrao: log_AAAAMMDD_HHMMSS.txt)")
    args = parser.parse_args()

    outfile = args.out or f"log_{datetime.datetime.now():%Y%m%d_%H%M%S}.txt"
    print(f"Gravando {args.port} @ {args.baud} baud em '{outfile}' (Ctrl+C pra parar)\n")

    try:
        with serial.Serial(args.port, args.baud, timeout=1) as ser, open(outfile, "a", encoding="utf-8") as f:
            while True:
                try:
                    line = ser.readline()
                except serial.SerialException as e:
                    print(f"\nErro na porta serial: {e}")
                    break
                if not line:
                    continue  # timeout sem dado novo, só continua esperando
                text = line.decode(errors="replace").rstrip()
                stamp = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
                out_line = f"{stamp} {text}"
                print(out_line)
                f.write(out_line + "\n")
                f.flush()
    except KeyboardInterrupt:
        print("\nParado pelo usuario.")
    except serial.SerialException as e:
        print(f"Nao consegui abrir {args.port}: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
