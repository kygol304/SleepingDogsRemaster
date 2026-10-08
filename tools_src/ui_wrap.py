"""Remplace l'écran Flash (CFX/GFX) contenu dans un fichier d'écran Sleeping Dogs (Data\\UI\\Screens\\*.bin).

L'en-tête de 0x78 octets est repris de l'original, avec les tailles mises à jour :
  +0x04 / +0x08 : taille du bloc (fichier - 16), +0x68 : taille de l'écran Flash.

    python ui_wrap.py <ecran_original.bin> <nouvel_ecran.gfx> <sortie.bin>
"""
import struct, sys

orig, gfx_path, out = sys.argv[1:4]
header = bytearray(open(orig, 'rb').read()[:0x78])
gfx = open(gfx_path, 'rb').read()
body = gfx + b'\0' * ((-len(gfx)) % 16)
total = len(header) + len(body)
struct.pack_into('<I', header, 0x04, total - 16)
struct.pack_into('<I', header, 0x08, total - 16)
struct.pack_into('<I', header, 0x68, len(gfx))
open(out, 'wb').write(bytes(header) + body)
print(out, total, 'octets (ecran Flash :', len(gfx), ')')
