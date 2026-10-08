// ===== Sleeping Dogs Remaster : ligne « ARMES » dans la garde-robe ========================
// Ajouté à la fin du script principal de Data\UI\Screens\wardrobe.bin (frame 1).
//  - une catégorie ARMES est ajoutée après les catégories du jeu (garde-robe seulement, pas les
//    boutiques) ;
//  - quand elle est surlignée, on prévient le plugin (ExternalInterface « GTA_ARM_HOVER », renvoyé
//    toutes les ~0,5 s tant qu'elle l'est) : A ouvre alors l'armurerie du mod ;
//  - le panneau de l'armurerie (_root.gtaArm*, écrit par le plugin) est dessiné ici, par-dessus la
//    liste de droite.


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

var GTA_ARMES = "ARMES";
var GTA_ARMES_ICON = "img://Icons_Weapon_9MM";
var gtaCatSample = "";

// Les catégories du jeu ont leur icône dans leur texte HTML (<img ...>) : on reprend le même
// format pour ARMES, avec l'icône du pistolet à la place.
function gtaArmesCaption()
{
   gtaDetectLang();
   GTA_ARMES = gtaTr("ARMES", "WEAPONS");
   // Les catégories sont des clés de traduction (...) : l'icône est dans le texte traduit.
   var s = com.utils.Localizer.LocalizeString(gtaCatSample);
   flash.external.ExternalInterface.call("GTA_LOG", "texte traduit : " + s);
   var i = s.indexOf("<img");
   if (i < 0) return GTA_ARMES;
   var j = s.indexOf(">", i);
   var tag = s.substring(i, j + 1);
   var k = tag.indexOf("src=");
   if (k >= 0) {
      var q = tag.charAt(k + 4);
      var e = tag.indexOf(q, k + 5);
      tag = tag.substring(0, k + 5) + GTA_ARMES_ICON + tag.substring(e);
   }
   return s.substring(0, i) + tag + " " + GTA_ARMES;
}
var gtaCatExpected = 0;
var gtaCatCount = 0;

// Le jeu peut annoncer le nombre de catégories avant ou après les avoir ajoutées :
// ARMES est ajoutée dès que les deux sont connus (une seule fois).
var gtaArmesAdded = false;
function gtaTryAddArmes()
{
   if (!IsStore && !gtaArmesAdded && gtaCatExpected > 0 && gtaCatCount >= gtaCatExpected) {
      gtaArmesAdded = true;
      gtaOrigCategoryAdd(gtaArmesCaption());
   }
}

var gtaOrigSetNumCategories = CategoryMenu_SetNumCategories;
CategoryMenu_SetNumCategories = function(num)
{
   gtaCatExpected = num;
   gtaOrigSetNumCategories(IsStore ? num : num + 1);
   gtaTryAddArmes();
};

var gtaOrigCategoryAdd = CategoryMenu_AddItem;
CategoryMenu_AddItem = function(caption)
{
   gtaOrigCategoryAdd(caption);
   if (gtaCatSample == "") gtaCatSample = caption;
   flash.external.ExternalInterface.call("GTA_LOG", "categorie : " + caption);
   gtaCatCount++;
   gtaTryAddArmes();
};

// Filet de sécurité : si ARMES n'a pas encore été ajoutée quand la liste s'affiche, on l'ajoute là.
var gtaOrigShowCategorySelect = ShowCategorySelect;
ShowCategorySelect = function()
{
   if (!IsStore && !gtaArmesAdded) {
      gtaArmesAdded = true;
      gtaOrigCategoryAdd(gtaArmesCaption());
   }
   gtaOrigShowCategorySelect();
};

function gtaArmHover(v)
{
   flash.external.ExternalInterface.call("GTA_ARM_HOVER", v);
}

var gtaOrigOutro = outro;
outro = function()
{
   gtaArmHover(0);
   gtaOrigOutro();
};

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

function gtaText(mc, name, x, y, w, size)
{
   mc.createTextField(name, mc.getNextHighestDepth(), x - w / 2, y - size, w, size * 2);
   var tf = mc[name];
   tf.embedFonts = true;
   tf.selectable = false;
   var f = mcTitle.getTextFormat();
   f.size = size;
   f.align = "center";
   f.color = 0xFFFFFF;
   tf.setNewTextFormat(f);
   return tf;
}

// Panneau de l'armurerie, sur la zone de la liste de droite.
_root.gtaArmOpen = 0;
var GTA_ARM_ROWS = 9;
var gtaArm = this.createEmptyMovieClip("gtaArm", 20010);
gtaArm._x = 1140;
gtaArm._y = 238;
gtaRect(gtaArm.createEmptyMovieClip("bg", 1), 0, 0, 690, 647, 0x0A0A0A, 96);
gtaRect(gtaArm.createEmptyMovieClip("line", 2), 0, 0, 690, 3, 0xF0B040, 100);
gtaArm.title = gtaText(gtaArm, "title", 345, 52, 640, 30);
gtaArm.hint = gtaText(gtaArm, "hint", 345, 622, 640, 18);
var gtaArmRows = [];
for (var ri = 0; ri < GTA_ARM_ROWS; ri++) {
   var row = gtaArm.createEmptyMovieClip("row" + ri, 10 + ri);
   row._y = 96 + ri * 54;
   row.bg = row.createEmptyMovieClip("bg", 1);
   gtaRect(row.bg, 40, 0, 610, 46, 0xFFFFFF, 100);
   row.label = gtaText(row, "label", 345, 24, 590, 22);
   gtaArmRows.push(row);
}
gtaArm._alpha = 0;

var gtaHoverLast = -1;
var gtaArmOpenLast = -1;
var gtaHoverFrames = 0;
var gtaWr = this.createEmptyMovieClip("gtaWr", 20011);
gtaWr.onEnterFrame = function()
{
   var hov = (gState == STATE_CATEGORY_SELECT && mc_CategoryMenu._visible && String(gCategoryMenu_SelectedValue).indexOf(GTA_ARMES) >= 0) ? 1 : 0;
   // Chaque image : le plugin lit la manette et met à jour le panneau (_root.gtaArm*).
   flash.external.ExternalInterface.call("GTA_ARM_TICK", hov);
   var open = _root.gtaArmOpen == 1;
   if (_root.gtaArmOpen != gtaArmOpenLast) {
      gtaArmOpenLast = _root.gtaArmOpen;
      flash.external.ExternalInterface.call("GTA_LOG", "panneau gtaArmOpen=" + _root.gtaArmOpen + " titre=" + _root.gtaArmTitle);
   }
   gtaArm._alpha = open ? 100 : 0;
   if (!open) return;
   gtaArm.title.text = _root.gtaArmTitle == undefined ? "" : _root.gtaArmTitle.toUpperCase();
   gtaArm.hint.text = _root.gtaArmHint == undefined ? "" : _root.gtaArmHint;
   var rows = [_root.gtaArmR0, _root.gtaArmR1, _root.gtaArmR2, _root.gtaArmR3, _root.gtaArmR4,
               _root.gtaArmR5, _root.gtaArmR6, _root.gtaArmR7, _root.gtaArmR8];
   for (var i = 0; i < GTA_ARM_ROWS; i++) {
      var r = gtaArmRows[i];
      var txt = rows[i] == undefined ? "" : rows[i];
      r._visible = txt != "";
      r.label.text = txt;
      var sel = _root.gtaArmSel == i;
      var c = new Color(r.bg);
      c.setRGB(sel ? 0xF0B040 : 0x1E1E1E);
      r.bg._alpha = sel ? 90 : 80;
      r.label.textColor = sel ? 0x111111 : 0xE8E8E8;
   }
};
