#!/usr/bin/env python3
"""
Conecta no NetLog do ESP32_Clock (telnet, porta 23) pela rede e salva os
logs num arquivo de texto com timestamp — sem precisar de cabo USB.

So funciona depois que o relogio ja conectou no WiFi (o servidor de log
so sobe nesse momento). Pra problemas de boot/conexao WiFi, ainda precisa
do capture_usb_log.py ou do Serial Monitor normal.

Uso:
    python capture_net_log.py esp32clock.local
    python capture_net_log.py 192.168.1.50 --out meu_log.txt

Nao precisa instalar nada (so usa a biblioteca padrao do Python).
"""
import argparse
import datetime
import socket
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("host", help="IP ou hostname do relogio (ex: esp32clock.local ou 192.168.1.50)")
    parser.add_argument("--port", type=int, default=23, help="Porta do NetLog (padrao: 23)")
    parser.add_argument("--out", default=None, help="Arquivo de saida (padrao: netlog_AAAAMMDD_HHMMSS.txt)")
    args = parser.parse_args()

    outfile = args.out or f"netlog_{datetime.datetime.now():%Y%m%d_%H%M%S}.txt"
    print(f"Conectando em {args.host}:{args.port}, gravando em '{outfile}' (Ctrl+C pra parar)\n")

    try:
        with socket.create_connection((args.host, args.port), timeout=10) as sock, \
             open(outfile, "a", encoding="utf-8") as f:
            sock.settimeout(1.0)
            buffer = b""
            while True:
                try:
                    chunk = sock.recv(256)
                except socket.timeout:
                    continue
                if not chunk:
                    print("\nConexao fechada pelo relogio (só aceita 1 cliente por vez — confira se não tem outro conectado).")
                    break
                buffer += chunk
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    text = line.decode(errors="replace").rstrip("\r")
                    stamp = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
                    out_line = f"{stamp} {text}"
                    print(out_line)
                    f.write(out_line + "\n")
                    f.flush()
    except KeyboardInterrupt:
        print("\nParado pelo usuario.")
    except (socket.timeout, ConnectionRefusedError, socket.gaierror) as e:
        print(f"Nao consegui conectar em {args.host}:{args.port}: {e}")
        print("Confira se o relogio ja conectou no WiFi (o log de rede só sobe depois disso).")
        sys.exit(1)


if __name__ == "__main__":
    main()
