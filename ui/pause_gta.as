// ===== Sleeping Dogs Remaster : ligne « CAMÉRA » dans les options du menu pause ============
// Ajouté à la fin du script principal de Data\UI\Screens\PauseMenu.bin (frame 1).
//  - le sous-menu OPTIONS (Audio, Affichage, Jeu, Commandes) reçoit une ligne CAMÉRA en dernier ;
//  - à chaque image, le plugin est prévenu (« GTA_CAM_TICK », 1 si CAMÉRA est surlignée) : A / Entrée
//    ouvre alors le réglage du champ de vision par situation, piloté par le plugin ;
//  - le panneau (_root.gtaCam*, écrit par le plugin) est dessiné ici, à la place du menu pause.

var gtaCamFr = true;
// Texte écrit comme les lignes du jeu (« Audio », « Affichage »). Le « é » est construit par son code :
// écrit tel quel dans le script, il s'affichait « CAMÃ‰RA » dans le menu.
var GTA_CAM_FR = "Cam" + String.fromCharCode(233) + "ra";
var gtaCamCaption = GTA_CAM_FR;
var gtaCamSubCount = 0;        // lignes ajoutées par le jeu au sous-menu depuis le dernier vidage
var gtaCamAdded = false;       // CAMÉRA déjà ajoutée à ce sous-menu

// Le sous-menu en cours de remplissage est celui des options si la ligne principale surlignée est
// OPTIONS (texte traduit : « OPTIONS » en français comme en anglais).
function gtaCamInOptions()
{
   var v = PauseMenu.GetSelectedValue();
   if (v == undefined) return false;
   return String(v).toUpperCase().indexOf("OPTION") >= 0;
}

// Langue : les lignes des options sont traduites par le jeu (« AFFICHAGE » / « DISPLAY »).
function gtaCamDetectLang(caption)
{
   var c = String(caption).toUpperCase();
   if (c.indexOf("AFFICHAGE") >= 0 || c.indexOf("COMMANDES") >= 0 || c.indexOf("JEU") >= 0) gtaCamFr = true;
   else if (c.indexOf("DISPLAY") >= 0 || c.indexOf("CONTROLS") >= 0 || c.indexOf("GAME") >= 0) gtaCamFr = false;
   gtaCamCaption = gtaCamFr ? GTA_CAM_FR : "Camera";
}

var gtaCamOrigClearSub = PauseMenu.ClearSubMenu;
PauseMenu.ClearSubMenu = function()
{
   gtaCamSubCount = 0;
   gtaCamAdded = false;
   gtaCamOrigClearSub.apply(PauseMenu, arguments);
};

var gtaCamOrigAddSub = PauseMenu.AddSubMenuItem;
PauseMenu.AddSubMenuItem = function(item, enabled)
{
   gtaCamOrigAddSub.apply(PauseMenu, arguments);
   gtaCamSubCount++;
   if (gtaCamInOptions()) gtaCamDetectLang(item);
};

// CAMÉRA est ajoutée juste avant l'ouverture du sous-menu des options, après les lignes du jeu.
function gtaCamAddLine()
{
   if (gtaCamAdded || gtaCamSubCount == 0 || !gtaCamInOptions()) return;
   gtaCamAdded = true;
   gtaCamOrigAddSub.apply(PauseMenu, [gtaCamCaption, true]);
   if (PauseMenu.mNumSubSlots == gtaCamSubCount) PauseMenu.SetNumVisualSubSlots(gtaCamSubCount + 1);
   flash.external.ExternalInterface.call("GTA_LOG", "menu pause : ligne " + gtaCamCaption + " ajoutee aux options (" + gtaCamSubCount + " lignes du jeu)");
}

var gtaCamOrigEnterSub = PauseMenu.EnterSubMenu;
PauseMenu.EnterSubMenu = function()
{
   gtaCamAddLine();
   gtaCamOrigEnterSub.apply(PauseMenu, arguments);
};

var gtaCamOrigSetVisualSub = PauseMenu.SetNumVisualSubSlots;
PauseMenu.SetNumVisualSubSlots = function(num)
{
   if (gtaCamAdded && num == gtaCamSubCount) num = gtaCamSubCount + 1;
   gtaCamOrigSetVisualSub.apply(PauseMenu, [num]);
};

function gtaCamHovered()
{
   return PauseMenu.mSubMenuActive && gtaCamAdded && PauseMenu.GetSubMenuSelectedValue() == gtaCamCaption;
}

// ===== Panneau du réglage (style des menus du jeu) ========================================
var GTA_CAM_ROWS = 10;
var GTA_CAM_W = 760;
var GTA_CAM_GOLD = 0xF2B33D;

function gtaCamRect(mc, x, y, w, h, color, alpha)
{
   mc.beginFill(color, alpha);
   mc.moveTo(x, y);
   mc.lineTo(x + w, y);
   mc.lineTo(x + w, y + h);
   mc.lineTo(x, y + h);
   mc.lineTo(x, y);
   mc.endFill();
}

function gtaCamText(mc, name, x, y, w, size, align)
{
   mc.createTextField(name, mc.getNextHighestDepth(), x, y - size, w, size * 2);
   var tf = mc[name];
   tf.embedFonts = true;
   tf.selectable = false;
   var f = mc_header.mc_inner.tf_header.getTextFormat();
   f.size = size;
   f.align = align;
   f.color = 0xFFFFFF;
   f.bold = false;
   tf.setNewTextFormat(f);
   return tf;
}

var gtaCam = _root.createEmptyMovieClip("gtaCam", 16030);
gtaCam._x = 140;
gtaCam._y = 190;
var gtaCamBg = gtaCam.createEmptyMovieClip("bg", 1);
gtaCamBg.beginFill(0x0A0A0A, 86);
gtaCamBg.lineStyle(2, 0x5E5E5E, 100);
gtaCamBg.moveTo(0, 0);
gtaCamBg.lineTo(GTA_CAM_W, 0);
gtaCamBg.lineTo(GTA_CAM_W, 700);
gtaCamBg.lineTo(0, 700);
gtaCamBg.lineTo(0, 0);
gtaCamBg.endFill();
gtaCam.title = gtaCamText(gtaCam, "title", 0, -40, GTA_CAM_W, 38, "left");
gtaCam.sub = gtaCamText(gtaCam, "sub", 0, -4, GTA_CAM_W, 16, "left");
var gtaCamSel = gtaCam.createEmptyMovieClip("sel", 5);
gtaCamSel.lineStyle(2, GTA_CAM_GOLD, 100);
gtaCamSel.beginFill(GTA_CAM_GOLD, 10);
gtaCamSel.moveTo(6, 0);
gtaCamSel.lineTo(GTA_CAM_W - 86, 0);
gtaCamSel.curveTo(GTA_CAM_W - 80, 0, GTA_CAM_W - 80, 6);
gtaCamSel.lineTo(GTA_CAM_W - 80, 38);
gtaCamSel.curveTo(GTA_CAM_W - 80, 44, GTA_CAM_W - 86, 44);
gtaCamSel.lineTo(6, 44);
gtaCamSel.curveTo(0, 44, 0, 38);
gtaCamSel.lineTo(0, 6);
gtaCamSel.curveTo(0, 0, 6, 0);
gtaCamSel.endFill();
gtaCamSel._x = 40;
gtaCamSel.filters = [new flash.filters.GlowFilter(GTA_CAM_GOLD, 0.75, 14, 14, 1.6, 2)];
var gtaCamRows = [];
for (var ci = 0; ci < GTA_CAM_ROWS; ci++) {
   var cr = gtaCam.createEmptyMovieClip("row" + ci, 10 + ci);
   cr._y = 40 + ci * 52;
   cr.label = gtaCamText(cr, "label", 60, 22, 420, 20, "left");
   cr.value = gtaCamText(cr, "value", 440, 22, 220, 20, "right");
   gtaCamRows.push(cr);
}
var gtaCamFoot = gtaCam.createEmptyMovieClip("foot", 3);
gtaCamFoot.lineStyle(2, 0x4A4A4A, 100);
gtaCamFoot.beginFill(0x000000, 60);
gtaCamFoot.moveTo(22, 624);
gtaCamFoot.lineTo(GTA_CAM_W - 22, 624);
gtaCamFoot.lineTo(GTA_CAM_W - 22, 678);
gtaCamFoot.lineTo(22, 678);
gtaCamFoot.lineTo(22, 624);
gtaCamFoot.endFill();
gtaCam.hint = gtaCamText(gtaCam, "hint", 46, 651, GTA_CAM_W - 92, 16, "left");
gtaCam._visible = false;
var gtaCamWasOpen = false;

var gtaCamCtl = _root.createEmptyMovieClip("gtaCamCtl", 16031);
gtaCamCtl.onEnterFrame = function()
{
   flash.external.ExternalInterface.call("GTA_CAM_TICK", gtaCamHovered() ? 1 : 0);
   var open = _root.gtaCamOpen == 1;
   gtaCam._visible = open;
   if (open != gtaCamWasOpen) {
      gtaCamWasOpen = open;
      PauseMenu._visible = !open;
      mc_info._visible = !open;
   }
   if (!open) return;
   gtaCam.title.text = String(_root.gtaCamTitle == undefined ? "" : _root.gtaCamTitle).toUpperCase();
   gtaCam.sub.text = String(_root.gtaCamSub == undefined ? "" : _root.gtaCamSub).toUpperCase();
   gtaCam.hint.text = String(_root.gtaCamHint == undefined ? "" : _root.gtaCamHint).toUpperCase();
   var hintFmt = gtaCam.hint.getTextFormat();
   hintFmt.color = GTA_CAM_GOLD;
   gtaCam.hint.setTextFormat(hintFmt);
   gtaCamSel._visible = false;
   for (var i = 0; i < GTA_CAM_ROWS; i++) {
      var r = gtaCamRows[i];
      var txt = _root["gtaCamR" + i];
      if (txt == undefined) txt = "";
      r._visible = txt != "";
      var bar = String(txt).indexOf("|");
      r.label.text = String(bar >= 0 ? txt.substring(0, bar) : txt).toUpperCase();
      r.value.text = bar >= 0 ? txt.substring(bar + 1) : "";
      var sel = _root.gtaCamSel == i;
      r.label.textColor = sel ? 0xFFFFFF : 0xD8D8D8;
      r.value.textColor = sel ? GTA_CAM_GOLD : 0xD8D8D8;
      if (sel && txt != "") {
         gtaCamSel._visible = true;
         gtaCamSel._y = r._y;
      }
   }
};
