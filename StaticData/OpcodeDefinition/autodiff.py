import json
import os
import socket
import sys
import urllib.request


def single(obj):
    it = iter(obj)
    x = next(it)
    try:
        next(it)
        raise RuntimeError("More than 1 item found")
    except StopIteration:
        return x


def __main__(argv):
    sf, tf, rf = argv[1:4]
    if not os.path.exists(sf):
        print(f"{sf} does not exist.")
        return -1
    if tf == "-":
        with open(r"C:\Program Files (x86)\SquareEnix\FINAL FANTASY XIV - A Realm Reborn\game\ffxivgame.ver") as tf:
            tf = f"{os.path.basename(sf)[:8]}{tf.read()}.json"
    if os.path.exists(tf):
        print(f"{tf} already exists.")
        return -1
    with open(sf, "rb") as sf:
        sf = json.load(sf)
    if rf.lower().startswith("http://") or rf.lower().startswith("https://"):
        with urllib.request.urlopen(urllib.request.Request(
                rf,
                headers={
                    "User-Agent": "Mozilla/5.0"
                })) as rf:
            rf = json.load(rf)
    else:
        with open(rf, "rb") as rf:
            rf = json.load(rf)
    data = {}
    for k, v in sf.items():
        if k.startswith("C2S_") or k.startswith("S2C_"):
            v = int(v, 0)
            entry = single(x for x in rf if len(x["old"]) == 1 and int(x["old"][0], 0) == v)
            entry = int(single(entry["new"]), 0)
            data[k] = f'0x{entry:04x}'
        elif k == "Server_IpRange":
            ips = {"119.252.37.0/24"}
            try:
                for i in range(1, 100):
                    ips.add(".".join(socket.gethostbyname(f"neolobby{i:02}.ffxiv.com").split(".")[:3]) + ".0/24")
            except socket.gaierror:
                pass
            data[k] = ", ".join(sorted(ips))
        else:
            data[k] = v
    with open(tf, "w", encoding="utf-8") as tf:
        json.dump(data, tf, indent='\t')
    return 0


if __name__ == "__main__":
    exit(__main__(sys.argv))
