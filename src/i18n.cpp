#include "i18n.h"

#include "common.h"

#include <array>
#include <cwctype>

namespace i18n {
namespace {

constexpr int kStrCount = static_cast<int>(Str::Count);
using Table = std::array<const wchar_t*, kStrCount>;

// Order must match enum class Str. Every language must define every string (checked by the tests).
// clang-format off
constexpr std::array<Table, kLanguageCount> kStrings = {{
    // English
    {L"CPU", L"Memory (RAM)", L"GPU", L"GPU memory (VRAM)", L"Update interval", L"{0} s",
     L"Highlight high load (amber/red)", L"Open Task Manager on click", L"Start with Windows", L"Language",
     L"Automatic (system)", L"Open Task Manager", L"About sys-bar {0}", L"Exit sys-bar",
     L"Performance", L"CPU", L"Memory", L"GPU", L"GPU memory", L"of {0} · {1} in use", L"max. {0}",
     L"{0} threads", L"last {0} s", L"last {0} min", L"Click: Task Manager", L"Right-click: options", L"GB",
     L"{0}%"},
    // Português (Brasil)
    {L"CPU", L"Memória RAM", L"GPU", L"Memória da GPU (VRAM)", L"Intervalo de atualização",
     L"{0} s", L"Destacar carga alta (âmbar/vermelho)", L"Abrir o Gerenciador de Tarefas ao clicar",
     L"Iniciar com o Windows", L"Idioma", L"Automático (sistema)", L"Abrir o Gerenciador de Tarefas",
     L"Sobre o sys-bar {0}", L"Sair do sys-bar",
     L"Desempenho", L"CPU", L"Memória", L"GPU", L"Memória da GPU", L"de {0} · {1} em uso",
     L"máx. {0}", L"{0} threads", L"últimos {0} s", L"últimos {0} min",
     L"Clique: Gerenciador de Tarefas", L"Botão direito: opções", L"GB", L"{0}%"},
    // Español
    {L"CPU", L"Memoria (RAM)", L"GPU", L"Memoria de GPU (VRAM)", L"Intervalo de actualización", L"{0} s",
     L"Resaltar carga alta (ámbar/rojo)", L"Abrir el Administrador de tareas al hacer clic",
     L"Iniciar con Windows", L"Idioma", L"Automático (sistema)", L"Abrir el Administrador de tareas",
     L"Acerca de sys-bar {0}", L"Salir de sys-bar",
     L"Rendimiento", L"CPU", L"Memoria", L"GPU", L"Memoria de GPU", L"de {0} · {1} en uso",
     L"máx. {0}", L"{0} hilos", L"últimos {0} s", L"últimos {0} min",
     L"Clic: Administrador de tareas", L"Clic derecho: opciones", L"GB", L"{0}%"},
    // Français
    {L"Processeur (CPU)", L"Mémoire (RAM)", L"GPU", L"Mémoire GPU (VRAM)",
     L"Intervalle d’actualisation", L"{0} s", L"Mettre en évidence la charge élevée (ambre/rouge)",
     L"Ouvrir le Gestionnaire des tâches au clic", L"Démarrer avec Windows", L"Langue",
     L"Automatique (système)", L"Ouvrir le Gestionnaire des tâches", L"À propos de sys-bar {0}",
     L"Quitter sys-bar",
     L"Performances", L"Processeur", L"Mémoire", L"GPU", L"Mémoire GPU",
     L"sur {0} · {1} utilisés", L"max. {0}", L"{0} threads", L"dernières {0} s",
     L"dernières {0} min", L"Clic : Gestionnaire des tâches", L"Clic droit : options", L"Go",
     L"{0} %"},
    // Deutsch
    {L"CPU", L"Arbeitsspeicher (RAM)", L"GPU", L"GPU-Speicher (VRAM)", L"Aktualisierungsintervall", L"{0} s",
     L"Hohe Auslastung hervorheben (Orange/Rot)", L"Task-Manager per Klick öffnen", L"Mit Windows starten",
     L"Sprache", L"Automatisch (System)", L"Task-Manager öffnen", L"Über sys-bar {0}", L"sys-bar beenden",
     L"Leistung", L"CPU", L"Arbeitsspeicher", L"GPU", L"GPU-Speicher", L"von {0} · {1} belegt",
     L"max. {0}", L"{0} Threads", L"letzte {0} s", L"letzte {0} min", L"Klick: Task-Manager",
     L"Rechtsklick: Optionen", L"GB", L"{0} %"},
    // Italiano
    {L"CPU", L"Memoria (RAM)", L"GPU", L"Memoria GPU (VRAM)", L"Intervallo di aggiornamento", L"{0} s",
     L"Evidenzia carico elevato (ambra/rosso)", L"Apri Gestione attività al clic", L"Avvia con Windows",
     L"Lingua", L"Automatica (sistema)", L"Apri Gestione attività", L"Informazioni su sys-bar {0}",
     L"Esci da sys-bar",
     L"Prestazioni", L"CPU", L"Memoria", L"GPU", L"Memoria GPU", L"di {0} · {1} in uso", L"max {0}",
     L"{0} thread", L"ultimi {0} s", L"ultimi {0} min", L"Clic: Gestione attività", L"Clic destro: opzioni",
     L"GB", L"{0}%"},
    // Polski
    {L"CPU", L"Pamięć (RAM)", L"GPU", L"Pamięć GPU (VRAM)", L"Częstotliwość odświeżania",
     L"{0} s", L"Wyróżniaj wysokie obciążenie (bursztynowy/czerwony)",
     L"Otwieraj Menedżera zadań po kliknięciu", L"Uruchamiaj razem z systemem Windows", L"Język",
     L"Automatycznie (system)", L"Otwórz Menedżera zadań", L"O programie sys-bar {0}",
     L"Zamknij sys-bar",
     L"Wydajność", L"CPU", L"Pamięć", L"GPU", L"Pamięć GPU", L"z {0} · {1} w użyciu",
     L"maks. {0}", L"Wątki: {0}", L"ostatnie {0} s", L"ostatnie {0} min", L"Kliknięcie: Menedżer zadań",
     L"Prawy przycisk: opcje", L"GB", L"{0}%"},
    // Türkçe
    {L"CPU", L"Bellek (RAM)", L"GPU", L"GPU belleği (VRAM)", L"Güncelleme aralığı", L"{0} sn",
     L"Yüksek yükü vurgula (kehribar/kırmızı)", L"Tıklayınca Görev Yöneticisi'ni aç",
     L"Windows ile başlat", L"Dil", L"Otomatik (sistem)", L"Görev Yöneticisi'ni aç",
     L"sys-bar hakkında {0}", L"sys-bar'dan çık",
     L"Performans", L"CPU", L"Bellek", L"GPU", L"GPU belleği", L"{0} içinde · {1} kullanımda",
     L"en fazla {0}", L"{0} iş parçacığı", L"son {0} sn", L"son {0} dk",
     L"Tıklama: Görev Yöneticisi", L"Sağ tık: seçenekler", L"GB", L"%{0}"},
    // Русский
    {L"ЦП (CPU)", L"Память (RAM)", L"ГП (GPU)",
     L"Память ГП (VRAM)", L"Интервал обновления",
     L"{0} с",
     L"Выделять высокую нагрузку (жёлтым/красным)",
     L"Открывать диспетчер задач по щелчку",
     L"Запускать вместе с Windows", L"Язык",
     L"Автоматически (системный)",
     L"Открыть диспетчер задач",
     L"О программе sys-bar {0}", L"Выйти из sys-bar",
     L"Производительность", L"ЦП",
     L"Память", L"ГП", L"Память ГП",
     L"из {0} · используется {1}", L"макс. {0}",
     L"Потоков: {0}", L"последние {0} с",
     L"последние {0} мин",
     L"Щелчок: диспетчер задач",
     L"Правый щелчок: параметры",
     L"ГБ", L"{0}%"},
    // Українська
    {L"ЦП (CPU)", L"Пам’ять (RAM)", L"ГП (GPU)",
     L"Пам’ять ГП (VRAM)", L"Інтервал оновлення",
     L"{0} с",
     L"Виділяти високе навантаження (жовтим/червоним)",
     L"Відкривати диспетчер завдань клацанням",
     L"Запускати разом із Windows", L"Мова",
     L"Автоматично (системна)",
     L"Відкрити диспетчер завдань",
     L"Про sys-bar {0}", L"Вийти з sys-bar",
     L"Продуктивність", L"ЦП",
     L"Пам’ять", L"ГП", L"Пам’ять ГП",
     L"з {0} · використано {1}", L"макс. {0}",
     L"Потоків: {0}", L"останні {0} с",
     L"останні {0} хв",
     L"Клацання: диспетчер завдань",
     L"Права кнопка: параметри",
     L"ГБ", L"{0}%"},
    // 日本語
    {L"CPU", L"メモリ (RAM)", L"GPU", L"GPU メモリ (VRAM)", L"更新間隔",
     L"{0} 秒", L"高負荷を強調表示 (黄/赤)",
     L"クリックでタスク マネージャーを開く",
     L"Windows の起動時に開始", L"言語", L"自動 (システム)",
     L"タスク マネージャーを開く", L"sys-bar について {0}",
     L"sys-bar を終了",
     L"パフォーマンス", L"CPU", L"メモリ", L"GPU", L"GPU メモリ",
     L"{0} 中 · {1} 使用中", L"最大 {0}", L"{0} スレッド",
     L"過去 {0} 秒", L"過去 {0} 分", L"クリック: タスク マネージャー",
     L"右クリック: オプション", L"GB", L"{0}%"},
    // 한국어
    {L"CPU", L"메모리(RAM)", L"GPU", L"GPU 메모리(VRAM)", L"업데이트 간격",
     L"{0}초", L"높은 부하 강조(주황/빨강)",
     L"클릭하면 작업 관리자 열기", L"Windows 시작 시 실행",
     L"언어", L"자동(시스템)", L"작업 관리자 열기",
     L"sys-bar 정보 {0}", L"sys-bar 종료",
     L"성능", L"CPU", L"메모리", L"GPU", L"GPU 메모리", L"{0} 중 · {1} 사용 중",
     L"최대 {0}", L"스레드 {0}개", L"최근 {0}초", L"최근 {0}분",
     L"클릭: 작업 관리자", L"오른쪽 클릭: 옵션", L"GB", L"{0}%"},
    // 简体中文
    {L"CPU", L"内存 (RAM)", L"GPU", L"GPU 内存 (VRAM)", L"更新间隔", L"{0} 秒",
     L"突出显示高负载（琥珀色/红色）",
     L"单击时打开任务管理器", L"随 Windows 启动", L"语言",
     L"自动（系统）", L"打开任务管理器", L"关于 sys-bar {0}",
     L"退出 sys-bar",
     L"性能", L"CPU", L"内存", L"GPU", L"GPU 内存", L"共 {0} · 已用 {1}",
     L"最高 {0}", L"{0} 个线程", L"最近 {0} 秒", L"最近 {0} 分钟",
     L"单击：任务管理器", L"右键：选项", L"GB", L"{0}%"},
    // 繁體中文
    {L"CPU", L"記憶體 (RAM)", L"GPU", L"GPU 記憶體 (VRAM)", L"更新間隔",
     L"{0} 秒", L"醒目提示高負載（琥珀色/紅色）",
     L"按一下時開啟工作管理員", L"隨 Windows 啟動", L"語言",
     L"自動（系統）", L"開啟工作管理員", L"關於 sys-bar {0}",
     L"結束 sys-bar",
     L"效能", L"CPU", L"記憶體", L"GPU", L"GPU 記憶體",
     L"共 {0} · 已使用 {1}", L"最高 {0}", L"{0} 個執行緒",
     L"最近 {0} 秒", L"最近 {0} 分鐘", L"按一下：工作管理員",
     L"按右鍵：選項", L"GB", L"{0}%"},
}};
// clang-format on

struct LangInfo {
    const wchar_t* code;
    const wchar_t* nativeName;
    const wchar_t* locale;
};

constexpr std::array<LangInfo, kLanguageCount> kInfo = {{
    {L"en", L"English", L"en-us"},
    {L"pt", L"Português", L"pt-br"},
    {L"es", L"Español", L"es-es"},
    {L"fr", L"Français", L"fr-fr"},
    {L"de", L"Deutsch", L"de-de"},
    {L"it", L"Italiano", L"it-it"},
    {L"pl", L"Polski", L"pl-pl"},
    {L"tr", L"Türkçe", L"tr-tr"},
    {L"ru", L"Русский", L"ru-ru"},
    {L"uk", L"Українська", L"uk-ua"},
    {L"ja", L"日本語", L"ja-jp"},
    {L"ko", L"한국어", L"ko-kr"},
    {L"zh-Hans", L"简体中文", L"zh-cn"},
    {L"zh-Hant", L"繁體中文", L"zh-tw"},
}};

std::wstring Lower(std::wstring_view s) {
    std::wstring out(s);
    for (auto& c : out) c = static_cast<wchar_t>(std::towlower(c));
    return out;
}

bool StartsWith(const std::wstring& s, const wchar_t* prefix) {
    return s.rfind(prefix, 0) == 0;
}

}  // namespace

Lang FromTag(std::wstring_view tag) {
    const std::wstring t = Lower(tag);
    if (StartsWith(t, L"zh")) {
        // Traditional script: explicit "Hant" or the regions that use it.
        if (t.find(L"hant") != std::wstring::npos || t.find(L"-tw") != std::wstring::npos ||
            t.find(L"-hk") != std::wstring::npos || t.find(L"-mo") != std::wstring::npos) {
            return Lang::ChineseTraditional;
        }
        return Lang::ChineseSimplified;
    }
    const std::wstring primary = t.substr(0, t.find(L'-'));
    for (int i = 0; i < kLanguageCount; ++i) {
        if (primary == kInfo[i].code) return static_cast<Lang>(i);
    }
    return Lang::English;
}

Lang FromSystem() {
    ULONG count = 0;
    ULONG size = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) && size > 0) {
        std::wstring buffer(size, L'\0');
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &size) && buffer[0]) {
            return FromTag(buffer.c_str());  // first entry of the double-null-terminated list
        }
    }
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0) return FromTag(name);
    return Lang::English;
}

const wchar_t* Code(Lang lang) {
    return kInfo[static_cast<int>(lang)].code;
}
const wchar_t* NativeName(Lang lang) {
    return kInfo[static_cast<int>(lang)].nativeName;
}
const wchar_t* Locale(Lang lang) {
    return kInfo[static_cast<int>(lang)].locale;
}

bool FromCode(std::wstring_view code, Lang& lang) {
    for (int i = 0; i < kLanguageCount; ++i) {
        if (code == kInfo[i].code) {
            lang = static_cast<Lang>(i);
            return true;
        }
    }
    return false;
}

const wchar_t* Get(Lang lang, Str id) {
    const int l = static_cast<int>(lang);
    const int s = static_cast<int>(id);
    if (l < 0 || l >= kLanguageCount || s < 0 || s >= kStrCount) return L"";
    const wchar_t* value = kStrings[l][s];
    return value ? value : kStrings[0][s];
}

}  // namespace i18n
