// ===== Sleeping Dogs Remaster : HUD façon GTA VI ==========================================
// Ajouté à la fin du script principal du HUD (Data\UI\Screens\Hud.bin, frame 1).
//  - barre de vie droite et fine sous la mini-carte (remplace l'arc autour de la carte)
//  - anneau décoratif de la mini-carte retiré
//  - argent et arme en haut à droite, avec des marges propres
//  - le HUD s'estompe quand rien ne se passe, et revient dès qu'il se passe quelque chose
// Coordonnées en pixels (écran 1920x1080).
// Mini-carte (plugin dinput8) : rectangle arrondi 301 x 215, centre (215, 930) une fois décalée
// de [Minimap] OffsetX=-40 / OffsetY=40. Bords : gauche 65, bas 1037.
// Barres sous la carte : même gauche, même largeur, vie puis réputation.

var GTA_BAR_X = 65;
var GTA_BAR_Y = 1042;
var GTA_BAR_W = 301;
var GTA_BAR_H = 7;
var GTA_RESPECT_Y = 1052;
var GTA_FOCUS_Y = 1062;
var GTA_COLOR_BG = 0x000000;
var GTA_COLOR_HEALTH = 0x6FC36F;
var GTA_COLOR_LOW = 0xD9534F;
var GTA_COLOR_RESPECT = 0xE2B340;
var GTA_COLOR_FOCUS = 0x3A9BFF;
var GTA_COLOR_FOCUS_ON = 0x8FD3FF;
var GTA_FADE_DELAY = 6000;      // ms sans évènement avant d'estomper
var GTA_FADE_ALPHA = 35;        // opacité quand le HUD est au repos

var gtaHealthMax = 100;
var gtaRespectMax = 100;
var gtaLastEvent = getTimer();
var gtaLastWeaponFrame = -1;
var GTA_COMBAT_DELAY = 8000;    // ms après le dernier coup avant de cacher la barre de vie
var gtaLastCombat = -100000;
var gtaLastHealth = -1;

function gtaCombat()
{
   gtaLastCombat = getTimer();
   gtaWake();
}

function gtaRect(mc, x, y, w, h, color, alpha)
{
   mc.beginFill(color, alpha);
   mc.moveTo(x, y);
   mc.lineTo(x + w, y);
   mc.lineTo(x + w, y + h);
   mc.lineTo(x, y + h);
   mc.lineTo(x, y);
   mc.endFill();
}

// Texte centré (police du HUD). Défini avant tout usage : les fonctions ne sont pas remontées en
// tête du script (le compteur, créé plus haut, n'avait pas de texte).
function gtaText(mc, name, x, y, w, size)
{
   mc.createTextField(name, mc.getNextHighestDepth(), x - w / 2, y - size, w, size * 2);
   var tf = mc[name];
   tf.embedFonts = true;
   tf.selectable = false;
   var f = mc_Money.tf_money.getTextFormat();
   f.size = size;
   f.align = "center";
   f.color = 0xFFFFFF;
   tf.setNewTextFormat(f);
   return tf;
}

function gtaWake()
{
   gtaLastEvent = getTimer();
}

// circle_mask ne se voit pas : le moteur y lit seulement le centre et le rayon de la mini-carte
// (icônes, zones de police). On le déplace comme la carte (même décalage que OffsetX/OffsetY).
var GTA_MAP_DX = -40;
var GTA_MAP_DY = 40;
circle_mask._x += GTA_MAP_DX;
circle_mask._y += GTA_MAP_DY;

var gtaHud = _root.createEmptyMovieClip("gtaHud", 16000);
var gtaBar = gtaHud.createEmptyMovieClip("bar", 1);
gtaRect(gtaBar.createEmptyMovieClip("bg", 1), 0, 0, GTA_BAR_W, GTA_BAR_H, GTA_COLOR_BG, 45);
var gtaFill = gtaBar.createEmptyMovieClip("fill", 2);
gtaRect(gtaFill, 0, 0, GTA_BAR_W, GTA_BAR_H, 0xFFFFFF, 100);
gtaBar._x = GTA_BAR_X;
gtaBar._y = GTA_BAR_Y;
gtaBar._alpha = 0;

var gtaRespect = gtaHud.createEmptyMovieClip("respect", 2);
gtaRect(gtaRespect.createEmptyMovieClip("bg", 1), 0, 0, GTA_BAR_W, GTA_BAR_H, GTA_COLOR_BG, 45);
var gtaRespectFill = gtaRespect.createEmptyMovieClip("fill", 2);
gtaRect(gtaRespectFill, 0, 0, GTA_BAR_W, GTA_BAR_H, 0xFFFFFF, 100);
gtaRespect._x = GTA_BAR_X;
gtaRespect._y = GTA_RESPECT_Y;
gtaRespect._alpha = 0;
var gtaRespectColor = new Color(gtaRespectFill);
gtaRespectColor.setRGB(GTA_COLOR_RESPECT);
gtaRespectFill._xscale = 0;

function gtaSetHealth(health)
{
   if (health > gtaHealthMax) gtaHealthMax = health;
   var ratio = health / gtaHealthMax;
   if (ratio < 0) ratio = 0;
   if (ratio > 1) ratio = 1;
   gtaFill._xscale = ratio * 100;
   var c = new Color(gtaFill);
   c.setRGB(ratio < 0.4 ? GTA_COLOR_LOW : GTA_COLOR_HEALTH);
}

// La fonction d'origine reste appelée (elle gère l'effet de vie basse, la régénération...),
// on met juste à jour notre barre en plus.
var gtaOriginalUpdateHealth = updateHealthMeter;
updateHealthMeter = function(health)
{
   gtaOriginalUpdateHealth(health);
   gtaSetHealth(health);
   if (gtaLastHealth >= 0 && health < gtaLastHealth) gtaCombat();   // coup reçu
   gtaLastHealth = health;
};
gtaSetHealth(gHealthValue === undefined ? gtaHealthMax : gHealthValue);

// Barre de focus (bleue, sous la jauge de combat) : valeur et état envoyés par le plugin
// (_root.gtaFocus 0..1, _root.gtaFocusOn) ; elle se remplit avec les gains de la jauge jaune.
var gtaFocusBar = gtaHud.createEmptyMovieClip("focus", 3);
gtaRect(gtaFocusBar.createEmptyMovieClip("bg", 1), 0, 0, GTA_BAR_W, GTA_BAR_H, GTA_COLOR_BG, 45);
var gtaFocusFill = gtaFocusBar.createEmptyMovieClip("fill", 2);
gtaRect(gtaFocusFill, 0, 0, GTA_BAR_W, GTA_BAR_H, 0xFFFFFF, 100);
gtaFocusBar._x = GTA_BAR_X;
gtaFocusBar._y = GTA_FOCUS_Y;
gtaFocusBar._alpha = 0;
var gtaFocusColor = new Color(gtaFocusFill);
gtaFocusColor.setRGB(GTA_COLOR_FOCUS);
gtaFocusFill._xscale = 0;
var gtaFocusWasOn = false;

// Compteur de vitesse (en bas à droite, en voiture : _root.gtaSpeed en km/h, -1 à pied) et, dessous,
// la barre de nitro des voitures équipées (_root.gtaNitro 0..1, _root.gtaNitroState 0 / 1 prête / 2 injection).
var GTA_SPEEDO_X = 500;    // à droite de la mini-carte (en bas à droite : cadre des défis du jeu)
var GTA_SPEEDO_Y = 950;
var gtaSpeedo = _root.createEmptyMovieClip("gtaSpeedo", 16023);   // hors de gtaHud : ses textes ne s'y affichaient pas
gtaSpeedo._x = GTA_SPEEDO_X;
gtaSpeedo._y = GTA_SPEEDO_Y;
gtaSpeedo.num = gtaText(gtaSpeedo, "num", 0, 20, 260, 46);
gtaSpeedo.unit = gtaText(gtaSpeedo, "unit", 0, 62, 260, 14);
gtaSpeedo.unit.text = "KM/H";
gtaSpeedo._visible = false;
var GTA_NITRO_W = 180;
var gtaNitroBar = gtaSpeedo.createEmptyMovieClip("nitro", 10);
gtaRect(gtaNitroBar.createEmptyMovieClip("bg", 1), 0, 0, GTA_NITRO_W, 8, GTA_COLOR_BG, 55);
var gtaNitroFill = gtaNitroBar.createEmptyMovieClip("fill", 2);
gtaRect(gtaNitroFill, 0, 0, GTA_NITRO_W, 8, 0xFFFFFF, 100);
gtaNitroBar._x = -GTA_NITRO_W / 2;
gtaNitroBar._y = 84;
gtaNitroBar._visible = false;
var gtaNitroColor = new Color(gtaNitroFill);
gtaNitroBar.label = gtaText(gtaSpeedo, "nitroLabel", 0, 106, 200, 12);

function gtaUpdateNitro()
{
   var speed = _root.gtaSpeed;
   gtaSpeedo._visible = speed != undefined && speed >= 0;
   if (!gtaSpeedo._visible) return;
   gtaSpeedo.num.text = String(speed);
   var st = _root.gtaNitroState;
   gtaNitroBar._visible = st == 1 || st == 2;
   gtaNitroBar.label.text = gtaNitroBar._visible ? "NITRO" : "";
   if (!gtaNitroBar._visible) return;
   var v = _root.gtaNitro;
   if (v == undefined) v = 0;
   gtaNitroFill._xscale = Math.max(0, Math.min(1, v)) * 100;
   gtaNitroColor.setRGB(st == 2 ? 0xFFD27F : 0xFF8A1E);
}

function gtaUpdateFocus(barAlpha)
{
   var v = _root.gtaFocus;
   if (v == undefined) v = 0;
   gtaFocusFill._xscale = Math.max(0, Math.min(1, v)) * 100;
   var on = _root.gtaFocusOn == 1;
   if (on != gtaFocusWasOn) {
      gtaFocusWasOn = on;
      gtaFocusColor.setRGB(on ? GTA_COLOR_FOCUS_ON : GTA_COLOR_FOCUS);
      if (on) gtaCombat();
   }
   var a = on ? 100 : barAlpha;
   gtaFocusBar._alpha = a;
   gtaFocusBar._visible = a > 0;
}

function gtaSetRespect(value)
{
   if (mc_CombatMeter._totalframes > gtaRespectMax) gtaRespectMax = mc_CombatMeter._totalframes;
   if (value > gtaRespectMax) gtaRespectMax = value;
   var ratio = value / gtaRespectMax;
   if (ratio < 0) ratio = 0;
   if (ratio > 1) ratio = 1;
   gtaRespectFill._xscale = ratio * 100;
}

var gtaOriginalCombat = CombatMeter_Update;
CombatMeter_Update = function(oldCombatValue, combatValue, meterActivated)
{
   gtaOriginalCombat(oldCombatValue, combatValue, meterActivated);
   gtaSetRespect(combatValue);
   if (combatValue != oldCombatValue) gtaCombat();                  // coup donné
   // Gain de la jauge jaune : la barre de focus gagne la même part.
   if (combatValue > oldCombatValue && gtaRespectMax > 0)
      flash.external.ExternalInterface.call("GTA_FOCUS_GAIN", (combatValue - oldCombatValue) / gtaRespectMax);
};
gtaSetRespect(0);

// Le jeu réaffiche l'ancien HUD en sortie de cinématique ou de menu (alpha forcé
// pendant l'intro, CombatMeter_Intro, pulse). On le coupe tout de suite, pas
// seulement à l'image suivante.
function gtaHideVanilla()
{
   mcOuterRing._visible = false;
   mcOuterRing._alpha = 0;
   mc_HealthBar._visible = false;
   mc_HealthBar._alpha = 0;
   mc_CombatMeter._visible = false;
   mc_CombatMeter._alpha = 0;
   mc_MinimapPulse._visible = false;
   mc_MinimapPulse._alpha = 0;
   if (mc_minimapBorder != undefined) {
      mc_minimapBorder._visible = false;
      mc_minimapBorder._alpha = 0;
   }
}
gtaHideVanilla();

var gtaOriginalCombatIntro = CombatMeter_Intro;
CombatMeter_Intro = function()
{
   gtaOriginalCombatIntro();
   gtaHideVanilla();
};

var gtaOriginalMinimapPulse = Cops_SetMinimapPulse;
Cops_SetMinimapPulse = function(heatLevel, isCooldown)
{
   gtaOriginalMinimapPulse(heatLevel, isCooldown);
   gtaHideVanilla();
};

var gtaOriginalPing = Cops_PingMinimap;
Cops_PingMinimap = function()
{
   gtaOriginalPing();
   gtaHideVanilla();
};

var gtaOriginalUndim = undim;
undim = function()
{
   gtaOriginalUndim();
   gtaHideVanilla();
};

gtaHud.onEnterFrame = function()
{
   gtaHideVanilla();

   // Disposition en haut à droite : argent, puis l'arme juste en dessous.
   mc_Money._y = 24;
   mc_WeaponSwitcher._y = 58;

   // Évènements qui réveillent le HUD : changement d'arme, police, combat, vie basse.
   var wf = mc_WeaponSwitcher._currentframe;
   if (wf != gtaLastWeaponFrame) { gtaLastWeaponFrame = wf; gtaWake(); }
   if (mcHeatMeter._visible) gtaWake();
   if (gtaLastHealth >= 0 && gtaFill._xscale < 40) gtaCombat();     // vie basse : la barre reste

   var now = getTimer();
   var target = (now - gtaLastEvent < GTA_FADE_DELAY) ? 100 : GTA_FADE_ALPHA;
   var a = mc_Money._alpha + (target - mc_Money._alpha) * 0.12;
   mc_Money._alpha = a;
   mc_WeaponSwitcher._alpha = a;

   // Barres de vie et de réputation : seulement en combat, puis elles disparaissent.
   var barTarget = (now - gtaLastCombat < GTA_COMBAT_DELAY) ? 100 : 0;
   var b = gtaBar._alpha + (barTarget - gtaBar._alpha) * (barTarget > 0 ? 0.3 : 0.08);
   if (b < 1) b = 0;
   gtaBar._alpha = b;
   gtaRespect._alpha = b;
   gtaBar._visible = b > 0;
   gtaRespect._visible = b > 0;
   gtaUpdateFocus(b);
   gtaUpdateNitro();

   gtaPickupRefresh();
};

// Invite « Maintenir RB : échanger contre … » sur une arme du mod au sol (GameplayHelp_Show, avec
// l'icône de l'arme) : le jeu y met le nom et l'icône de l'arme d'origine (_root.gtaPickupFrom, ex.
// « Fusil d'assaut ») ; on met celui du mod (gtaPickupTo ; l'image de l'icône est remplacée par le plugin). Nom d'origine introuvable
// (autre langue) : on remplace ce qui suit « contre » / « for ».
function gtaPickupReplace(txt)
{
   var to = _root.gtaPickupTo;
   if (txt == undefined || to == undefined || to == "") return txt;
   var orig = txt;
   txt = String(txt);
   if (txt.charAt(0) == "$") txt = com.utils.Localizer.LocalizeString(txt);
   var upTxt = txt.toUpperCase();
   var name = (upTxt == txt) ? to.toUpperCase() : to;
   if (upTxt.indexOf(to.toUpperCase()) >= 0) return orig;
   var from = String(_root.gtaPickupFrom).toUpperCase();
   var i = from.length > 0 ? upTxt.indexOf(from) : -1;
   if (i >= 0) return txt.substr(0, i) + name + txt.substr(i + from.length);
   // « Échanger contre X », « Prendre X » (le jeu n'a pas le même nom que la table du mod).
   var seps = [" CONTRE ", "PRENDRE ", "RAMASSER ", " FOR ", "PICK UP ", "TAKE "];
   for (var k = 0; k < seps.length; k++) {
      var j = upTxt.lastIndexOf(seps[k]);
      if (j >= 0) return txt.substr(0, j + seps[k].length) + name;
   }
   return orig;
}

var gtaOriginalHelpShow = GameplayHelp_Show;
GameplayHelp_Show = function(button0, helpTxt0, icon0, isHold0, button1, helpTxt1, icon1, isHold1)
{
   var t0 = gtaPickupReplace(helpTxt0);
   var t1 = gtaPickupReplace(helpTxt1);
   // L'icône garde son nom : le plugin remplace son image (à la bonne taille).
   var i0 = icon0;
   var i1 = icon1;
   gtaHelpArgs = [button0, helpTxt0, icon0, isHold0, button1, helpTxt1, icon1, isHold1];
   gtaHelpShown = t0 + "|" + t1;
   gtaOriginalHelpShow(button0, t0, i0, isHold0, button1, t1, i1, isHold1);
};
var gtaHelpArgs = undefined;
var gtaHelpShown = "";
var gtaOriginalHelpHide = GameplayHelp_Hide;
GameplayHelp_Hide = function()
{
   gtaHelpArgs = undefined;
   gtaOriginalHelpHide();
};

// Le nom du mod peut arriver une image après l'invite : elle est refaite si son texte change.
function gtaPickupRefresh()
{
   if (gtaHelpArgs == undefined || !mc_GameplayHelp._visible || _root.gtaPickupTo == undefined || _root.gtaPickupTo == "") return;
   var a = gtaHelpArgs;
   if (gtaPickupReplace(a[1]) + "|" + gtaPickupReplace(a[5]) == gtaHelpShown) return;
   GameplayHelp_Show(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
}


// ===== Langue du jeu ======================================================================
// Le traducteur du jeu donne « HAUTS » pour $ITEMCATEGORY_BODY en français : on en déduit la
// langue (français, sinon anglais) et on prévient le plugin (« GTA_LANG »).
var gtaLangFr = true;
var gtaLangKnown = false;
function gtaDetectLang()
{
   if (gtaLangKnown) return true;
   var s = com.utils.Localizer.LocalizeString("$ITEMCATEGORY_BODY");
   if (s == undefined || s == "" || s.indexOf("ITEMCATEGORY") >= 0) return false;
   gtaLangFr = s.toUpperCase().indexOf("HAUT") >= 0;
   gtaLangKnown = true;
   flash.external.ExternalInterface.call("GTA_LANG", gtaLangFr ? "fr" : "en");
   return true;
}
function gtaTr(fr, en) { return gtaLangFr ? fr : en; }

// ===== Roue d'arme (façon GTA V / RDR2) ===================================================
// L'état est écrit par le plugin (dinput8.dll) dans _root.gtaWheelOpen / gtaWheelSel /
// gtaWheelL0..L3 / gtaWheelHand. Cases : 0 haut mains nues, 1 gauche ceinture,
// 2 droite arme longue 1, 3 bas arme longue 2.
_root.gtaWheelOpen = 0;
_root.gtaWheelSel = -1;
var GTA_WHEEL_X = 960;
var GTA_WHEEL_Y = 540;
var GTA_WHEEL_R = 250;
var gtaWheelPos = [[0, -165], [-250, 0], [250, 0], [0, 165]];
var gtaWheelFmt = mc_Money.tf_money.getTextFormat();

function gtaCircle(mc, r, color, alpha)
{
   var k = 0.4142;
   var s = 0.7071;
   mc.beginFill(color, alpha);
   mc.moveTo(r, 0);
   mc.curveTo(r, k * r, s * r, s * r);
   mc.curveTo(k * r, r, 0, r);
   mc.curveTo(-k * r, r, -s * r, s * r);
   mc.curveTo(-r, k * r, -r, 0);
   mc.curveTo(-r, -k * r, -s * r, -s * r);
   mc.curveTo(-k * r, -r, 0, -r);
   mc.curveTo(k * r, -r, s * r, -s * r);
   mc.curveTo(r, -k * r, r, 0);
   mc.endFill();
}


var gtaWheel = _root.createEmptyMovieClip("gtaWheel", 16010);
gtaWheel._x = GTA_WHEEL_X;
gtaWheel._y = GTA_WHEEL_Y;
gtaCircle(gtaWheel.createEmptyMovieClip("disc", 1), GTA_WHEEL_R + 70, 0x000000, 55);
var gtaWheelBoxes = [];
for (var gi = 0; gi < 4; gi++) {
   var box = gtaWheel.createEmptyMovieClip("box" + gi, 10 + gi);
   box._x = gtaWheelPos[gi][0];
   box._y = gtaWheelPos[gi][1];
   box.frame = box.createEmptyMovieClip("frame", 0);
   gtaRect(box.frame, -124, -42, 248, 84, 0xF0B040, 100);
   box.frame._visible = false;
   var bg = box.createEmptyMovieClip("bg", 1);
   gtaRect(bg, -120, -38, 240, 76, 0xFFFFFF, 100);
   box.ico = box.createEmptyMovieClip("ico", 5);
   box.icoName = "";
   box.icoFit = false;
   box.label = gtaText(box, "label", 0, 0, 236, 22);
   gtaWheelBoxes.push(box);
}
gtaWheel.hand = gtaText(gtaWheel, "hand", 0, 0, 260, 20);
// Pointeur de la souris (clavier/souris) : _root.gtaWheelMouse, position gtaWheelMX / MY.
gtaWheel.cursor = gtaWheel.createEmptyMovieClip("cursor", 40);
gtaCircle(gtaWheel.cursor, 13, 0x000000, 75);
gtaCircle(gtaWheel.cursor, 9, 0xFFFFFF, 100);
gtaWheel.cursor._visible = false;
gtaWheel._alpha = 0;   // visible mais transparente : le plugin peut l'afficher directement

// Le HUD ne fait pas avancer les clips invisibles : la roue est pilotée par un clip vide,
// toujours visible, qui la montre ou la cache.
var gtaWheelCtl = _root.createEmptyMovieClip("gtaWheelCtl", 16011);
gtaWheelCtl.onEnterFrame = function()
{
   var w = gtaWheel;
   var open = _root.gtaWheelOpen == 1;
   // Fondu rapide à l'ouverture / fermeture (moins brutal qu'un affichage d'un coup).
   var target = open ? 100 : 0;
   w._alpha = w._alpha + (target - w._alpha) * 0.45;
   if (Math.abs(w._alpha - target) < 3) w._alpha = target;
   var labels = [_root.gtaWheelL0, _root.gtaWheelL1, _root.gtaWheelL2, _root.gtaWheelL3];
   var icons = [_root.gtaWheelI0, _root.gtaWheelI1, _root.gtaWheelI2, _root.gtaWheelI3];
   for (var i = 0; i < 4; i++) {
      var b = gtaWheelBoxes[i];
      var txt = labels[i];
      var empty = txt == undefined || txt == "";
      b.label.text = empty ? "-" : txt;
      // Icône de l'arme (texture du jeu « img://Icons_Weapon_... ») : un clip par icône, chargé
      // une seule fois puis réutilisé (recharger à chaque changement faisait clignoter la roue).
      var ico = icons[i];
      if (ico == undefined) ico = "";
      if (b.icons == undefined) b.icons = new Object();
      // Image pas encore arrivée au bout d'~2 s (ou vide) : on recrée le clip et on recharge.
      if (ico != "" && b.icons[ico] != undefined && !b.icons[ico].fit) {
         b.icons[ico].tries++;
         if (b.icons[ico].tries > 60) { b.icons[ico].removeMovieClip(); b.icons[ico] = undefined; }
      }
      if (ico != "" && b.icons[ico] == undefined) {
         b.icoSeq = b.icoSeq == undefined ? 1 : b.icoSeq + 1;
         var holder = b.createEmptyMovieClip("ico_" + ico + "_" + b.icoSeq, b.getNextHighestDepth());
         holder.fit = false;
         holder.tries = 0;
         holder._visible = false;
         holder.loadMovie("img://" + ico);
         b.icons[ico] = holder;
      }
      var shown = false;
      for (var name in b.icons) {
         var hc = b.icons[name];
         if (hc == undefined) continue;
         if (!hc.fit && hc._width > 4 && hc._height > 4) {
            var sc = Math.min(200 / hc._width, 62 / hc._height) * 100;
            hc._xscale = hc._yscale = sc;
            hc._x = -hc._width / 2;
            hc._y = -hc._height / 2;
            hc.fit = true;
         }
         var vis = name == ico && hc.fit;
         hc._visible = vis;
         if (vis) shown = true;
      }
      b.label._visible = !shown;
      if (!open && w._alpha == 0) continue;
      // Gris clair : la case visée au stick. Doré : l'arme actuellement en main.
      var sel = _root.gtaWheelSel == i;
      var cur = _root.gtaWheelCur == i;
      var c = new Color(b.bg);
      c.setRGB(sel ? 0x6A6A6A : (cur ? 0x3A2E12 : 0x1A1A1A));
      b.bg._alpha = sel ? 95 : 85;
      b.frame._visible = cur;
      b.label.textColor = empty ? 0x777777 : 0xFFFFFF;
      b._xscale = b._yscale = sel ? 108 : 100;
   }
   w.hand.text = _root.gtaWheelHand == undefined ? "" : _root.gtaWheelHand;
   w.cursor._visible = _root.gtaWheelMouse == 1;
   // Lissé : la position n'arrive qu'à chaque mise à jour du plugin, le pointeur la rejoint en douceur.
   if (w.cursor._visible) {
      if (!w.cursorOn) { w.cursor._x = _root.gtaWheelMX; w.cursor._y = _root.gtaWheelMY; }
      w.cursor._x += (_root.gtaWheelMX - w.cursor._x) * 0.5;
      w.cursor._y += (_root.gtaWheelMY - w.cursor._y) * 0.5;
   }
   w.cursorOn = w.cursor._visible && open;
};

// ===== Armurerie de planque (liste façon garde-robe) =======================================
// Écrit par le plugin : _root.gtaArmOpen, gtaArmTitle, gtaArmR0..R8 (lignes visibles), gtaArmSel.
_root.gtaArmOpen = 0;
var GTA_ARM_ROWS = 9;
// Style des boutiques du jeu (boutique de vêtements) : panneau sombre à fin liseré gris, grand titre
// au-dessus, lignes en capitales alignées à gauche, ligne choisie dans un cadre doré lumineux, bandeau
// en bas du panneau, rappel des touches en bas à gauche de l'écran.
var GTA_SHOP_W = 690;
var GTA_SHOP_H = 800;
var GTA_SHOP_GOLD = 0xF2B33D;

function gtaRoundRect(mc, x, y, w, h, r)
{
   mc.moveTo(x + r, y);
   mc.lineTo(x + w - r, y);
   mc.curveTo(x + w, y, x + w, y + r);
   mc.lineTo(x + w, y + h - r);
   mc.curveTo(x + w, y + h, x + w - r, y + h);
   mc.lineTo(x + r, y + h);
   mc.curveTo(x, y + h, x, y + h - r);
   mc.lineTo(x, y + r);
   mc.curveTo(x, y, x + r, y);
}

// Texte aligné à gauche (police du HUD).
function gtaTextL(mc, name, x, y, w, size)
{
   var tf = gtaText(mc, name, x + w / 2, y, w, size);
   var f = tf.getNewTextFormat();
   f.align = "left";
   tf.setNewTextFormat(f);
   return tf;
}

var gtaArm = _root.createEmptyMovieClip("gtaArm", 16020);
gtaArm._x = 1140;
gtaArm._y = 170;
var gtaShopBg = gtaArm.createEmptyMovieClip("bg", 1);
gtaShopBg.beginFill(0x0A0A0A, 84);
gtaShopBg.lineStyle(2, 0x5E5E5E, 100);
gtaShopBg.moveTo(0, 0);
gtaShopBg.lineTo(GTA_SHOP_W, 0);
gtaShopBg.lineTo(GTA_SHOP_W, GTA_SHOP_H);
gtaShopBg.lineTo(0, GTA_SHOP_H);
gtaShopBg.lineTo(0, 0);
gtaShopBg.endFill();
// Titre au-dessus du panneau, à droite, et sous-titre.
gtaArm.title = gtaText(gtaArm, "title", GTA_SHOP_W - 300, -62, 560, 38);
var gtaTitleFmt = gtaArm.title.getNewTextFormat();
gtaTitleFmt.align = "right";
gtaTitleFmt.color = 0xF6EBD2;
gtaArm.title.setNewTextFormat(gtaTitleFmt);
gtaArm.sub = gtaText(gtaArm, "sub", GTA_SHOP_W - 300, -18, 560, 16);
var gtaSubFmt = gtaArm.sub.getNewTextFormat();
gtaSubFmt.align = "right";
gtaArm.sub.setNewTextFormat(gtaSubFmt);
// Bandeau du bas : texte doré (aide), bordure grise, comme le bandeau de réputation.
var gtaShopFoot = gtaArm.createEmptyMovieClip("foot", 3);
gtaShopFoot.lineStyle(2, 0x4A4A4A, 100);
gtaShopFoot.beginFill(0x000000, 60);
gtaShopFoot.moveTo(22, GTA_SHOP_H - 76);
gtaShopFoot.lineTo(GTA_SHOP_W - 22, GTA_SHOP_H - 76);
gtaShopFoot.lineTo(GTA_SHOP_W - 22, GTA_SHOP_H - 22);
gtaShopFoot.lineTo(22, GTA_SHOP_H - 22);
gtaShopFoot.lineTo(22, GTA_SHOP_H - 76);
gtaShopFoot.endFill();
gtaArm.hint = gtaTextL(gtaArm, "hint", 46, GTA_SHOP_H - 49, GTA_SHOP_W - 92, 16);
var gtaHintFmt = gtaArm.hint.getNewTextFormat();
gtaHintFmt.color = GTA_SHOP_GOLD;
gtaArm.hint.setNewTextFormat(gtaHintFmt);
gtaArm.hint.text = "";
// Cadre de la ligne choisie : contour doré arrondi, fond doré très léger, halo.
var gtaShopSel = gtaArm.createEmptyMovieClip("sel", 5);
gtaShopSel.lineStyle(2, GTA_SHOP_GOLD, 100);
gtaShopSel.beginFill(GTA_SHOP_GOLD, 10);
gtaRoundRect(gtaShopSel, 0, 0, 310, 44, 6);
gtaShopSel.endFill();
gtaShopSel._x = 40;
gtaShopSel.filters = [new flash.filters.GlowFilter(GTA_SHOP_GOLD, 0.75, 14, 14, 1.6, 2)];
var gtaArmRows = [];
for (var ri = 0; ri < GTA_ARM_ROWS; ri++) {
   var row = gtaArm.createEmptyMovieClip("row" + ri, 10 + ri);
   row._y = 70 + ri * 48;
   row.label = gtaTextL(row, "label", 60, 22, GTA_SHOP_W - 120, 20);
   gtaArmRows.push(row);
}
// Rappel des touches, en bas à gauche de l'écran comme dans les boutiques.
var gtaShopKeys = _root.createEmptyMovieClip("gtaShopKeys", 16025);
gtaShopKeys.tf = gtaTextL(gtaShopKeys, "tf", 80, 950, 1000, 20);
gtaShopKeys._visible = false;
gtaArm._alpha = 0;

// Atelier : fondu au noir plein écran (_root.gtaFade 0..100), sous le menu.
var gtaFadeMc = _root.createEmptyMovieClip("gtaFadeMc", 16019);
gtaRect(gtaFadeMc, -200, -200, 2400, 1500, 0x000000, 100);
gtaFadeMc._alpha = 0;
gtaFadeMc._visible = false;

// Atelier : statistiques sous le menu (_root.gtaStatShow, gtaStatL0..3 libellés, gtaStatA0..3 avant,
// gtaStatB0..3 après, 0..1). Barre blanche = installé, verte = gain essayé, rouge = perte.
var GTA_STAT_ROWS = 4;
var gtaStats = _root.createEmptyMovieClip("gtaStats", 16024);
gtaStats._y = 170 + GTA_SHOP_H - 76 - 176;
var gtaStatRows = [];
for (var si = 0; si < GTA_STAT_ROWS; si++) {
   var sr = gtaStats.createEmptyMovieClip("stat" + si, 10 + si);
   sr._y = 14 + si * 40;
   sr.label = gtaTextL(sr, "label", 46, 14, 230, 15);
   sr.track = sr.createEmptyMovieClip("track", 2);
   gtaRect(sr.track, 290, 10, 350, 8, 0xFFFFFF, 18);
   sr.gain = sr.createEmptyMovieClip("gain", 3);
   gtaRect(sr.gain, 0, 0, 100, 8, 0xFFFFFF, 100);
   sr.gain._x = 290; sr.gain._y = 10;
   sr.base = sr.createEmptyMovieClip("base", 4);
   gtaRect(sr.base, 0, 0, 100, 8, 0xFFFFFF, 100);
   sr.base._x = 290; sr.base._y = 10;
   gtaStatRows.push(sr);
}
gtaStats._visible = false;

function gtaUpdateStats()
{
   var show = _root.gtaStatShow == 1 && _root.gtaArmOpen == 1;
   gtaStats._visible = show;
   if (!show) return;
   gtaStats._x = gtaArm._x;
   for (var i = 0; i < GTA_STAT_ROWS; i++) {
      var r = gtaStatRows[i];
      var a = _root["gtaStatA" + i];
      var b = _root["gtaStatB" + i];
      if (a == undefined) a = 0;
      if (b == undefined) b = a;
      a = Math.max(0, Math.min(1, a));
      b = Math.max(0, Math.min(1, b));
      r.label.text = _root["gtaStatL" + i] == undefined ? "" : _root["gtaStatL" + i];
      r.base._xscale = Math.min(a, b) * 350;
      r.gain._xscale = Math.max(a, b) * 350;
      new Color(r.gain).setRGB(b >= a ? 0x4CD964 : 0xE04040);
   }
}

// Garage de tuning : rappel en bas de l'écran quand la voiture est dans un garage (_root.gtaGarageHint).
var gtaGarageHintTf = gtaText(_root.createEmptyMovieClip("gtaGarageHintMc", 16022), "txt", 960, 900, 900, 20);

// Rappel sous la roue quand on est dans une planque.
gtaWheel.armHint = gtaText(gtaWheel, "armHint", 0, 345, 420, 16);

var gtaLangTry = 0;
var gtaArmCtl = _root.createEmptyMovieClip("gtaArmCtl", 16021);
gtaArmCtl.onEnterFrame = function()
{
   if (!gtaLangKnown && (gtaLangTry++ % 30) == 0) gtaDetectLang();
   gtaWheel.armHint.text = _root.gtaWheelArmory == 1 ? gtaTr("Planque - A : armurerie", "Safehouse - A: armory") : "";
   gtaGarageHintTf.text = (_root.gtaGarageHint == undefined || _root.gtaArmOpen == 1) ? "" : _root.gtaGarageHint;
   var fade = _root.gtaFade == undefined ? 0 : _root.gtaFade;
   gtaFadeMc._alpha = fade;
   gtaUpdateStats();
   gtaFadeMc._visible = fade > 0;
   var open = _root.gtaArmOpen == 1;
   gtaArm._alpha = open ? 100 : 0;
   gtaShopKeys._visible = open;
   if (!open) return;
   var garage = _root.gtaArmRaw == 1;
   gtaArm.title.text = String(garage ? gtaTr("Atelier", "Workshop") : gtaTr("Armurerie", "Armory")).toUpperCase();
   gtaArm.sub.text = String(_root.gtaArmTitle == undefined ? "" : _root.gtaArmTitle).toUpperCase();
   gtaArm.hint.text = String(_root.gtaArmHint == undefined ? "" : _root.gtaArmHint).toUpperCase();
   gtaShopKeys.tf.text = gtaTr("(A) SÉLECTION      (B) RETOUR", "(A) SELECT      (B) BACK");
   gtaShopSel._visible = false;
   var rows = [_root.gtaArmR0, _root.gtaArmR1, _root.gtaArmR2, _root.gtaArmR3, _root.gtaArmR4,
               _root.gtaArmR5, _root.gtaArmR6, _root.gtaArmR7, _root.gtaArmR8];
   for (var i = 0; i < GTA_ARM_ROWS; i++) {
      var r = gtaArmRows[i];
      var txt = rows[i] == undefined ? "" : rows[i];
      r._visible = txt != "";
      r.label.text = txt;
      var sel = _root.gtaArmSel == i;
      r.label.text = String(txt).toUpperCase();
      r.label.textColor = sel ? 0xFFFFFF : 0xD8D8D8;
      if (sel && txt != "") {
         gtaShopSel._visible = true;
         gtaShopSel._y = r._y;
         gtaShopSel._width = Math.min(GTA_SHOP_W - 80, Math.max(310, r.label.textWidth + 70));
      }
   }
};
