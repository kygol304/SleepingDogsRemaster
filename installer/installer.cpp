// Installateur de Sleeping Dogs Remaster.
//   - copie dinput8.dll (le mod) et SleepingDogsRemaster.ini (s'il n'existe pas) dans le dossier du jeu,
//     ainsi que le dossier SleepingDogsRemaster\ (modèles 3D et icônes des nouvelles armes) ;
//   - injecte les écrans Flash modifiés (HUD, garde-robe) dans UI.big / UI.bix : le contenu est ajouté
//     à la fin de UI.big et seule l'entrée correspondante de l'index UI.bix est modifiée ;
//   - sauvegarde UI.bix et la taille de UI.big au premier passage, pour pouvoir tout remettre.
// Désinstaller remet UI.bix et la taille de UI.big d'origine, et supprime le mod.
// Les fichiers à installer sont dans le dossier « files » à côté de cet exécutable.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const char* kGameExe = "sdhdship.exe";
const char* kBixBackup = "UI.bix.remaster_backup";
const char* kBigSize = "UI.big.remaster_size";
const char* kDataDir = "SleepingDogsRemaster";   // modèles et icônes des nouvelles armes

struct Screen { uint32_t uid; const char* file; };
const Screen kScreens[] = {
    { 0x4FAA1890, "Hud.bin" },        // Data\UI\Screens\Hud.bin
    { 0x53ACE131, "Wardrobe.bin" },   // Data\UI\Screens\wardrobe.bin
};

void Say(const char* fr, const char* en)
{
    printf("%s\n   (%s)\n", fr, en);
}

bool ReadAll(const fs::path& p, std::vector<uint8_t>& out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

bool WriteAll(const fs::path& p, const std::vector<uint8_t>& data)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

bool IsGameDir(const fs::path& d)
{
    std::error_code ec;
    return fs::exists(d / kGameExe, ec) && fs::exists(d / "UI.big", ec) && fs::exists(d / "UI.bix", ec);
}

// Dossiers Steam : le dossier par défaut et ceux de libraryfolders.vdf.
std::vector<fs::path> SteamLibraries()
{
    std::vector<fs::path> libs;
    char buf[MAX_PATH] = {};
    DWORD size = sizeof(buf);
    fs::path steam = "C:\\Program Files (x86)\\Steam";
    if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
        steam = buf;
    libs.push_back(steam);
    std::ifstream vdf(steam / "steamapps" / "libraryfolders.vdf");
    std::string line;
    while (std::getline(vdf, line)) {
        const auto k = line.find("\"path\"");
        if (k == std::string::npos) continue;
        const auto a = line.find('"', k + 6);
        const auto b = line.find('"', a + 1);
        if (a == std::string::npos || b == std::string::npos) continue;
        std::string p = line.substr(a + 1, b - a - 1);
        std::string clean;
        for (size_t i = 0; i < p.size(); ++i) {
            if (p[i] == '\\' && i + 1 < p.size() && p[i + 1] == '\\') ++i;
            clean += p[i];
        }
        libs.emplace_back(clean);
    }
    return libs;
}

fs::path FindGame(const fs::path& here)
{
    if (IsGameDir(here)) return here;
    if (IsGameDir(here.parent_path())) return here.parent_path();
    for (const auto& lib : SteamLibraries()) {
        const fs::path d = lib / "steamapps" / "common" / "SleepingDogsDefinitiveEdition";
        if (IsGameDir(d)) return d;
    }
    return {};
}

bool ArchivesLocked(const fs::path& game)
{
    HANDLE h = CreateFileW((game / "UI.big").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return true;
    CloseHandle(h);
    return false;
}

// Remet UI.bix et la taille de UI.big d'origine (si une sauvegarde existe).
bool RestoreArchives(const fs::path& game)
{
    std::error_code ec;
    if (!fs::exists(game / kBixBackup, ec)) return true;
    fs::copy_file(game / kBixBackup, game / "UI.bix", fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    std::ifstream sz(game / kBigSize);
    unsigned long long size = 0;
    if (sz >> size) fs::resize_file(game / "UI.big", size, ec);
    return !ec;
}

bool Install(const fs::path& game, const fs::path& files)
{
    std::error_code ec;
    // 1. le mod et ses réglages
    fs::copy_file(files / "dinput8.dll", game / "dinput8.dll", fs::copy_options::overwrite_existing, ec);
    if (ec) { Say("Impossible de copier dinput8.dll.", "Could not copy dinput8.dll."); return false; }
    if (!fs::exists(game / "SleepingDogsRemaster.ini", ec))
        fs::copy_file(files / "SleepingDogsRemaster.ini", game / "SleepingDogsRemaster.ini", ec);
    // Modèles et icônes des nouvelles armes (dossier SleepingDogsRemaster\ à côté du jeu).
    if (fs::is_directory(files / kDataDir, ec)) {
        fs::create_directories(game / kDataDir, ec);
        for (const auto& f : fs::directory_iterator(files / kDataDir, ec)) {
            if (!f.is_regular_file()) continue;
            fs::copy_file(f.path(), game / kDataDir / f.path().filename(), fs::copy_options::overwrite_existing, ec);
            if (ec) { Say("Impossible de copier les modeles des armes.", "Could not copy the weapon models."); return false; }
        }
    }

    // 2. sauvegarde des archives (une seule fois), puis retour à l'état d'origine avant d'injecter
    if (!fs::exists(game / kBixBackup, ec)) {
        // Sauvegardes laissées par l'outil de développement (UI.bix.orig + UI.big.orig = taille) :
        // ce sont les vrais originaux, le jeu actuel est peut-être déjà modifié.
        if (fs::exists(game / "UI.bix.orig", ec) && fs::exists(game / "UI.big.orig", ec)) {
            fs::copy_file(game / "UI.bix.orig", game / kBixBackup, ec);
            std::ifstream in(game / "UI.big.orig");
            unsigned long long size = 0;
            in >> size;
            std::ofstream(game / kBigSize) << size;
        } else {
            fs::copy_file(game / "UI.bix", game / kBixBackup, ec);
            std::ofstream(game / kBigSize) << fs::file_size(game / "UI.big", ec);
        }
    }
    if (!RestoreArchives(game)) { Say("Impossible de preparer UI.big / UI.bix.", "Could not prepare UI.big / UI.bix."); return false; }

    // 3. injection des écrans : ajout à la fin de UI.big (aligné sur 4 Ko), entrée de l'index modifiée
    std::vector<uint8_t> index;
    if (!ReadAll(game / "UI.bix", index)) { Say("UI.bix illisible.", "Cannot read UI.bix."); return false; }
    std::fstream big(game / "UI.big", std::ios::binary | std::ios::in | std::ios::out);
    if (!big) { Say("UI.big illisible.", "Cannot open UI.big."); return false; }
    big.seekp(0, std::ios::end);
    for (const auto& s : kScreens) {
        std::vector<uint8_t> data;
        if (!ReadAll(files / "ui" / s.file, data)) { Say("Fichier manquant dans files\\ui.", "Missing file in files\\ui."); return false; }
        size_t pos = std::string::npos;
        for (size_t i = 0; i + 24 <= index.size(); i += 4) {
            uint32_t v;
            memcpy(&v, &index[i], 4);
            if (v == s.uid) { pos = i; break; }
        }
        if (pos == std::string::npos) {
            Say("Version du jeu non reconnue (ecran absent de UI.bix).", "Unsupported game version (screen not found in UI.bix).");
            return false;
        }
        const uint64_t end = static_cast<uint64_t>(big.tellp());
        const uint64_t aligned = (end + 0xFFF) & ~0xFFFull;
        std::vector<char> pad(static_cast<size_t>(aligned - end), 0);
        big.write(pad.data(), static_cast<std::streamsize>(pad.size()));
        big.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        const uint32_t entry[6] = { s.uid, static_cast<uint32_t>(aligned / 4), 0, 0, 0, static_cast<uint32_t>(data.size()) };
        memcpy(&index[pos], entry, sizeof(entry));
        printf("   %s -> OK\n", s.file);
    }
    big.close();
    if (!WriteAll(game / "UI.bix", index)) { Say("Impossible d'ecrire UI.bix.", "Could not write UI.bix."); return false; }
    return true;
}

bool Uninstall(const fs::path& game, const fs::path& files)
{
    std::error_code ec;
    // Seulement les fichiers que l'installateur a posés, puis le dossier s'il est vide.
    for (const auto& f : fs::directory_iterator(files / kDataDir, ec))
        if (f.is_regular_file()) fs::remove(game / kDataDir / f.path().filename(), ec);
    if (fs::is_empty(game / kDataDir, ec)) fs::remove(game / kDataDir, ec);
    if (!RestoreArchives(game)) { Say("Impossible de restaurer UI.big / UI.bix.", "Could not restore UI.big / UI.bix."); return false; }
    fs::remove(game / kBixBackup, ec);
    fs::remove(game / kBigSize, ec);
    fs::remove(game / "dinput8.dll", ec);
    Say("Le fichier de reglages SleepingDogsRemaster.ini est conserve (supprime-le si tu veux).",
        "SleepingDogsRemaster.ini was kept (delete it if you want).");
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    printf("==============================================\n");
    printf("   Sleeping Dogs Remaster - installation\n");
    printf("==============================================\n\n");

    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    const fs::path here = fs::path(exePath).parent_path();
    const fs::path files = here / "files";

    fs::path game = argc > 1 ? fs::path(argv[1]) : FindGame(here);
    while (game.empty() || !IsGameDir(game)) {
        Say("Dossier du jeu introuvable. Colle ici le chemin du dossier de Sleeping Dogs: Definitive Edition :",
            "Game folder not found. Paste the path of the Sleeping Dogs: Definitive Edition folder:");
        printf("> ");
        std::string line;
        if (!std::getline(std::cin, line)) return 1;
        while (!line.empty() && (line.back() == '"' || line.back() == ' ' || line.back() == '\r')) line.pop_back();
        while (!line.empty() && (line.front() == '"' || line.front() == ' ')) line.erase(line.begin());
        game = line;
    }
    printf("Jeu / Game : %s\n\n", game.string().c_str());

    while (ArchivesLocked(game)) {
        Say("Le jeu est ouvert : ferme-le puis appuie sur Entree.", "The game is running: close it, then press Enter.");
        std::string dummy;
        std::getline(std::cin, dummy);
    }

    Say("1 = Installer le mod     2 = Desinstaller", "1 = Install the mod     2 = Uninstall");
    printf("> ");
    std::string choice;
    std::getline(std::cin, choice);
    // Ignorer espaces et retours chariot (réponse collée ou envoyée par un script).
    while (!choice.empty() && (choice.back() == '\r' || choice.back() == ' ')) choice.pop_back();
    while (!choice.empty() && choice.front() == ' ') choice.erase(choice.begin());
    bool ok;
    if (choice.find('2') != std::string::npos) {   // tolérant (octets invisibles en tête, etc.)
        ok = Uninstall(game, files);
        if (ok) Say("\nMod desinstalle, le jeu est revenu a l'origine.", "Mod uninstalled, the game is back to normal.");
    } else {
        ok = Install(game, files);
        if (ok) Say("\nInstallation terminee ! Lance le jeu depuis Steam.", "Installation complete! Launch the game from Steam.");
    }
    printf("\nAppuie sur Entree pour fermer / Press Enter to close.");
    std::string dummy;
    std::getline(std::cin, dummy);
    return ok ? 0 : 1;
}
