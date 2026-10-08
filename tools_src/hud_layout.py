"""Modifie la disposition du HUD (Hud.xml exporté par JPEXS) : position / échelle des éléments
nommés de la timeline principale. Unités : twips (1 pixel = 20 twips, écran 1920x1080 = 38400x21600).

    python hud_layout.py <Hud.xml> <sortie.xml>
"""
import sys
import xml.etree.ElementTree as ET

# nom -> (x, y, échelle) ; None = inchangé
LAYOUT = {
    # Test de la chaîne : argent un peu plus bas, arme en bas à droite.
    'mc_Money':          (None, 1500, None),
    'mc_WeaponSwitcher': (None, 17200, None),
}

src, dst = sys.argv[1:3]
tree = ET.parse(src)
for it in tree.getroot().find('tags'):
    name = it.get('name')
    if not it.get('type', '').startswith('PlaceObject') or name not in LAYOUT:
        continue
    x, y, scale = LAYOUT[name]
    m = it.find('matrix')
    if x is not None:
        m.set('translateX', str(x))
    if y is not None:
        m.set('translateY', str(y))
    if scale is not None:
        m.set('hasScale', 'true')
        m.set('scaleX', str(scale))
        m.set('scaleY', str(scale))
    print('modifie', name, x, y, scale)
tree.write(dst, encoding='utf-8', xml_declaration=True)
