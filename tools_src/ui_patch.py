"""Remplace des fichiers dans une archive .big de Sleeping Dogs sans la reconstruire.

Le nouveau contenu est ajouté à la fin de l'archive (aligné sur 4 Ko) et seule l'entrée
correspondante de l'index .bix est modifiée (position/4 + taille, stockage non compressé).
Les archives d'origine sont sauvegardées en .orig au premier usage ; chaque application
repart de ces originaux, donc l'outil est rejouable à volonté.

    python ui_patch.py apply  <dossier_jeu> <archive> <uid_hex>=<fichier> [...]
    python ui_patch.py restore <dossier_jeu> <archive>
"""
import os, shutil, struct, sys

ENTRY_SIZE = 24  # uid, offset/4, load_offset, compressed_size, compressed_extra, uncompressed_size


def paths(game, archive):
    big = os.path.join(game, archive + '.big')
    bix = os.path.join(game, archive + '.bix')
    return big, bix, big + '.orig', bix + '.orig'


def backup(game, archive):
    big, bix, big_o, bix_o = paths(game, archive)
    if not os.path.exists(bix_o):
        shutil.copy2(bix, bix_o)
    if not os.path.exists(big_o):
        # on garde seulement la taille d'origine : les ajouts sont tronqués à chaque application
        with open(big_o, 'w') as f:
            f.write(str(os.path.getsize(big)))


def restore(game, archive):
    big, bix, big_o, bix_o = paths(game, archive)
    if os.path.exists(bix_o):
        shutil.copy2(bix_o, bix)
    if os.path.exists(big_o):
        with open(big, 'r+b') as f:
            f.truncate(int(open(big_o).read()))
    print('archive', archive, 'restauree')


def apply(game, archive, replacements):
    backup(game, archive)
    big, bix, big_o, bix_o = paths(game, archive)
    index = bytearray(open(bix_o, 'rb').read())
    orig_size = int(open(big_o).read())
    with open(big, 'r+b') as f:
        f.truncate(orig_size)
        f.seek(0, os.SEEK_END)
        for uid, data in replacements:
            pos = index.find(struct.pack('<I', uid))
            while pos != -1 and pos % 4 != 0:
                pos = index.find(struct.pack('<I', uid), pos + 1)
            if pos == -1:
                raise SystemExit('identifiant 0x%08X absent de %s.bix' % (uid, archive))
            end = f.tell()
            aligned = (end + 0xFFF) & ~0xFFF
            f.write(b'\0' * (aligned - end))
            f.write(data)
            struct.pack_into('<6I', index, pos, uid, aligned // 4, 0, 0, 0, len(data))
            print('0x%08X -> %d octets a la position 0x%X' % (uid, len(data), aligned))
    open(bix, 'wb').write(index)


if __name__ == '__main__':
    cmd, game, archive = sys.argv[1:4]
    if cmd == 'restore':
        restore(game, archive)
    else:
        reps = []
        for arg in sys.argv[4:]:
            uid, path = arg.split('=', 1)
            reps.append((int(uid, 16), open(path, 'rb').read()))
        apply(game, archive, reps)
