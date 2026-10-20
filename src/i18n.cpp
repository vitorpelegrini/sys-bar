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
     L"{0}%",
     L"DISK", L"Disk", L"Show temperatures in the bar", L"Disk", L"Read {0} · Write {1}", L"MB/s", L"Sensors", L"{0} RPM", L"Updates", L"Check for updates now", L"Install updates automatically", L"Install version {0}", L"Download version {0}", L"You have the latest version ({0})", L"Checking for updates…", L"Downloading version {0}…", L"Update failed, will retry later", L"Version {0} available", L"Enable advanced sensors (CPU, fans)…", L"Advanced sensors active", L"Advanced sensors: CPU and fans (right-click)",
     L"Temperature unit", L"Automatic (region)", L"Disks", L"One column per disk", L"All disks combined",
     L"Case fan", L"Pump", L"PSU fan", L"Fan {0}", L"fan stopped",
     L"NET", L"BAT", L"Network", L"Down {0} · Up {1}", L"Network", L"Battery", L"KB/s", L"GB/s", L"Battery", L"Charging", L"On battery", L"Fully charged", L"Plugged in", L"{0} left", L"Fans", L"Automatic ({0})", L"Show on all taskbars", L"Up to date"},
    // Português (Brasil)
    {L"CPU", L"Memória RAM", L"GPU", L"Memória da GPU (VRAM)", L"Intervalo de atualização",
     L"{0} s", L"Destacar carga alta (âmbar/vermelho)", L"Abrir o Gerenciador de Tarefas ao clicar",
     L"Iniciar com o Windows", L"Idioma", L"Automático (sistema)", L"Abrir o Gerenciador de Tarefas",
     L"Sobre o sys-bar {0}", L"Sair do sys-bar",
     L"Desempenho", L"CPU", L"Memória", L"GPU", L"Memória da GPU", L"de {0} · {1} em uso",
     L"máx. {0}", L"{0} threads", L"últimos {0} s", L"últimos {0} min",
     L"Clique: Gerenciador de Tarefas", L"Botão direito: opções", L"GB", L"{0}%",
     L"DISCO", L"Disco", L"Mostrar temperaturas na barra", L"Disco", L"Leitura {0} · Gravação {1}", L"MB/s", L"Sensores", L"{0} RPM", L"Atualizações", L"Procurar atualizações agora", L"Instalar atualizações automaticamente", L"Instalar a versão {0}", L"Baixar a versão {0}", L"Você tem a versão mais recente ({0})", L"Procurando atualizações…", L"Baixando a versão {0}…", L"A atualização falhou; nova tentativa mais tarde", L"Versão {0} disponível", L"Ativar sensores avançados (CPU, fans)…", L"Sensores avançados ativos", L"Sensores avançados: CPU e fans (botão direito)",
     L"Unidade de temperatura", L"Automática (região)", L"Discos", L"Uma coluna por disco", L"Todos os discos juntos",
     L"Gabinete", L"Bomba", L"Fonte", L"Fan {0}", L"fan parada",
     L"REDE", L"BAT", L"Rede", L"Download {0} · Upload {1}", L"Rede", L"Bateria", L"KB/s", L"GB/s", L"Bateria", L"Carregando", L"Na bateria", L"Carga completa", L"Na tomada", L"restam {0}", L"Fans", L"Automático ({0})", L"Mostrar em todas as barras de tarefas", L"Atualizado"},
    // Español
    {L"CPU", L"Memoria (RAM)", L"GPU", L"Memoria de GPU (VRAM)", L"Intervalo de actualización", L"{0} s",
     L"Resaltar carga alta (ámbar/rojo)", L"Abrir el Administrador de tareas al hacer clic",
     L"Iniciar con Windows", L"Idioma", L"Automático (sistema)", L"Abrir el Administrador de tareas",
     L"Acerca de sys-bar {0}", L"Salir de sys-bar",
     L"Rendimiento", L"CPU", L"Memoria", L"GPU", L"Memoria de GPU", L"de {0} · {1} en uso",
     L"máx. {0}", L"{0} hilos", L"últimos {0} s", L"últimos {0} min",
     L"Clic: Administrador de tareas", L"Clic derecho: opciones", L"GB", L"{0}%",
     L"DISCO", L"Disco", L"Mostrar temperaturas en la barra", L"Disco", L"Lectura {0} · Escritura {1}", L"MB/s", L"Sensores", L"{0} RPM", L"Actualizaciones", L"Buscar actualizaciones ahora", L"Instalar actualizaciones automáticamente", L"Instalar la versión {0}", L"Descargar la versión {0}", L"Tienes la versión más reciente ({0})", L"Buscando actualizaciones…", L"Descargando la versión {0}…", L"La actualización falló; se reintentará más tarde", L"Versión {0} disponible", L"Activar sensores avanzados (CPU, ventiladores)…", L"Sensores avanzados activos", L"Sensores avanzados: CPU y ventiladores (clic derecho)",
     L"Unidad de temperatura", L"Automática (región)", L"Discos", L"Una columna por disco", L"Todos los discos juntos",
     L"Caja", L"Bomba", L"Fuente", L"Ventilador {0}", L"ventilador detenido",
     L"RED", L"BAT", L"Red", L"Bajada {0} · Subida {1}", L"Red", L"Batería", L"KB/s", L"GB/s", L"Batería", L"Cargando", L"Con batería", L"Carga completa", L"Conectado", L"quedan {0}", L"Ventiladores", L"Automático ({0})", L"Mostrar en todas las barras de tareas", L"Actualizado"},
    // Français
    {L"Processeur (CPU)", L"Mémoire (RAM)", L"GPU", L"Mémoire GPU (VRAM)",
     L"Intervalle d’actualisation", L"{0} s", L"Mettre en évidence la charge élevée (ambre/rouge)",
     L"Ouvrir le Gestionnaire des tâches au clic", L"Démarrer avec Windows", L"Langue",
     L"Automatique (système)", L"Ouvrir le Gestionnaire des tâches", L"À propos de sys-bar {0}",
     L"Quitter sys-bar",
     L"Performances", L"Processeur", L"Mémoire", L"GPU", L"Mémoire GPU",
     L"sur {0} · {1} utilisés", L"max. {0}", L"{0} threads", L"dernières {0} s",
     L"dernières {0} min", L"Clic : Gestionnaire des tâches", L"Clic droit : options", L"Go",
     L"{0} %",
     L"DISQUE", L"Disque", L"Afficher les températures dans la barre", L"Disque", L"Lecture {0} · Écriture {1}", L"Mo/s", L"Capteurs", L"{0} tr/min", L"Mises à jour", L"Rechercher les mises à jour", L"Installer les mises à jour automatiquement", L"Installer la version {0}", L"Télécharger la version {0}", L"Vous avez la dernière version ({0})", L"Recherche de mises à jour…", L"Téléchargement de la version {0}…", L"La mise à jour a échoué ; nouvel essai plus tard", L"Version {0} disponible", L"Activer les capteurs avancés (CPU, ventilateurs)…", L"Capteurs avancés actifs", L"Capteurs avancés : CPU et ventilateurs (clic droit)",
     L"Unité de température", L"Automatique (région)", L"Disques", L"Une colonne par disque", L"Tous les disques ensemble",
     L"Boîtier", L"Pompe", L"Alimentation", L"Ventilateur {0}", L"ventilateur à l'arrêt",
     L"RÉSEAU", L"BAT", L"Réseau", L"Réception {0} · Envoi {1}", L"Réseau", L"Batterie", L"Ko/s", L"Go/s", L"Batterie", L"En charge", L"Sur batterie", L"Chargée", L"Branché", L"{0} restantes", L"Ventilateurs", L"Automatique ({0})", L"Afficher sur toutes les barres des tâches", L"À jour"},
    // Deutsch
    {L"CPU", L"Arbeitsspeicher (RAM)", L"GPU", L"GPU-Speicher (VRAM)", L"Aktualisierungsintervall", L"{0} s",
     L"Hohe Auslastung hervorheben (Orange/Rot)", L"Task-Manager per Klick öffnen", L"Mit Windows starten",
     L"Sprache", L"Automatisch (System)", L"Task-Manager öffnen", L"Über sys-bar {0}", L"sys-bar beenden",
     L"Leistung", L"CPU", L"Arbeitsspeicher", L"GPU", L"GPU-Speicher", L"von {0} · {1} belegt",
     L"max. {0}", L"{0} Threads", L"letzte {0} s", L"letzte {0} min", L"Klick: Task-Manager",
     L"Rechtsklick: Optionen", L"GB", L"{0} %",
     L"DISK", L"Datenträger", L"Temperaturen in der Leiste anzeigen", L"Datenträger", L"Lesen {0} · Schreiben {1}", L"MB/s", L"Sensoren", L"{0} U/min", L"Updates", L"Jetzt nach Updates suchen", L"Updates automatisch installieren", L"Version {0} installieren", L"Version {0} herunterladen", L"Sie haben die neueste Version ({0})", L"Suche nach Updates…", L"Version {0} wird heruntergeladen…", L"Update fehlgeschlagen, neuer Versuch später", L"Version {0} verfügbar", L"Erweiterte Sensoren aktivieren (CPU, Lüfter)…", L"Erweiterte Sensoren aktiv", L"Erweiterte Sensoren: CPU und Lüfter (Rechtsklick)",
     L"Temperatureinheit", L"Automatisch (Region)", L"Datenträger", L"Eine Spalte pro Datenträger", L"Alle Datenträger zusammen",
     L"Gehäuse", L"Pumpe", L"Netzteil", L"Lüfter {0}", L"Lüfter steht",
     L"NETZ", L"AKKU", L"Netzwerk", L"Empfangen {0} · Senden {1}", L"Netzwerk", L"Akku", L"KB/s", L"GB/s", L"Akku", L"Wird geladen", L"Akkubetrieb", L"Vollständig geladen", L"Angeschlossen", L"noch {0}", L"Lüfter", L"Automatisch ({0})", L"Auf allen Taskleisten anzeigen", L"Aktuell"},
    // Italiano
    {L"CPU", L"Memoria (RAM)", L"GPU", L"Memoria GPU (VRAM)", L"Intervallo di aggiornamento", L"{0} s",
     L"Evidenzia carico elevato (ambra/rosso)", L"Apri Gestione attività al clic", L"Avvia con Windows",
     L"Lingua", L"Automatica (sistema)", L"Apri Gestione attività", L"Informazioni su sys-bar {0}",
     L"Esci da sys-bar",
     L"Prestazioni", L"CPU", L"Memoria", L"GPU", L"Memoria GPU", L"di {0} · {1} in uso", L"max {0}",
     L"{0} thread", L"ultimi {0} s", L"ultimi {0} min", L"Clic: Gestione attività", L"Clic destro: opzioni",
     L"GB", L"{0}%",
     L"DISCO", L"Disco", L"Mostra le temperature nella barra", L"Disco", L"Lettura {0} · Scrittura {1}", L"MB/s", L"Sensori", L"{0} RPM", L"Aggiornamenti", L"Cerca aggiornamenti ora", L"Installa gli aggiornamenti automaticamente", L"Installa la versione {0}", L"Scarica la versione {0}", L"Hai la versione più recente ({0})", L"Ricerca aggiornamenti…", L"Download della versione {0}…", L"Aggiornamento non riuscito, nuovo tentativo più tardi", L"Versione {0} disponibile", L"Attiva i sensori avanzati (CPU, ventole)…", L"Sensori avanzati attivi", L"Sensori avanzati: CPU e ventole (clic destro)",
     L"Unità di temperatura", L"Automatica (area geografica)", L"Dischi", L"Una colonna per disco", L"Tutti i dischi insieme",
     L"Case", L"Pompa", L"Alimentatore", L"Ventola {0}", L"ventola ferma",
     L"RETE", L"BAT", L"Rete", L"Ricezione {0} · Invio {1}", L"Rete", L"Batteria", L"KB/s", L"GB/s", L"Batteria", L"In carica", L"A batteria", L"Carica completa", L"Collegato", L"{0} rimanenti", L"Ventole", L"Automatico ({0})", L"Mostra su tutte le barre delle applicazioni", L"Aggiornato"},
    // Polski
    {L"CPU", L"Pamięć (RAM)", L"GPU", L"Pamięć GPU (VRAM)", L"Częstotliwość odświeżania",
     L"{0} s", L"Wyróżniaj wysokie obciążenie (bursztynowy/czerwony)",
     L"Otwieraj Menedżera zadań po kliknięciu", L"Uruchamiaj razem z systemem Windows", L"Język",
     L"Automatycznie (system)", L"Otwórz Menedżera zadań", L"O programie sys-bar {0}",
     L"Zamknij sys-bar",
     L"Wydajność", L"CPU", L"Pamięć", L"GPU", L"Pamięć GPU", L"z {0} · {1} w użyciu",
     L"maks. {0}", L"Wątki: {0}", L"ostatnie {0} s", L"ostatnie {0} min", L"Kliknięcie: Menedżer zadań",
     L"Prawy przycisk: opcje", L"GB", L"{0}%",
     L"DYSK", L"Dysk", L"Pokazuj temperatury na pasku", L"Dysk", L"Odczyt {0} · Zapis {1}", L"MB/s", L"Czujniki", L"{0} obr./min", L"Aktualizacje", L"Sprawdź aktualizacje teraz", L"Instaluj aktualizacje automatycznie", L"Zainstaluj wersję {0}", L"Pobierz wersję {0}", L"Masz najnowszą wersję ({0})", L"Sprawdzanie aktualizacji…", L"Pobieranie wersji {0}…", L"Aktualizacja nie powiodła się, ponowna próba później", L"Dostępna wersja {0}", L"Włącz zaawansowane czujniki (CPU, wentylatory)…", L"Zaawansowane czujniki aktywne", L"Zaawansowane czujniki: CPU i wentylatory (prawy przycisk)",
     L"Jednostka temperatury", L"Automatycznie (region)", L"Dyski", L"Jedna kolumna na dysk", L"Wszystkie dyski razem",
     L"Obudowa", L"Pompa", L"Zasilacz", L"Wentylator {0}", L"wentylator zatrzymany",
     L"SIEĆ", L"BAT", L"Sieć", L"Pobieranie {0} · Wysyłanie {1}", L"Sieć", L"Bateria", L"KB/s", L"GB/s", L"Bateria", L"Ładowanie", L"Na baterii", L"W pełni naładowana", L"Podłączono", L"pozostało {0}", L"Wentylatory", L"Automatycznie ({0})", L"Pokaż na wszystkich paskach zadań", L"Aktualna wersja"},
    // Türkçe
    {L"CPU", L"Bellek (RAM)", L"GPU", L"GPU belleği (VRAM)", L"Güncelleme aralığı", L"{0} sn",
     L"Yüksek yükü vurgula (kehribar/kırmızı)", L"Tıklayınca Görev Yöneticisi'ni aç",
     L"Windows ile başlat", L"Dil", L"Otomatik (sistem)", L"Görev Yöneticisi'ni aç",
     L"sys-bar hakkında {0}", L"sys-bar'dan çık",
     L"Performans", L"CPU", L"Bellek", L"GPU", L"GPU belleği", L"{0} içinde · {1} kullanımda",
     L"en fazla {0}", L"{0} iş parçacığı", L"son {0} sn", L"son {0} dk",
     L"Tıklama: Görev Yöneticisi", L"Sağ tık: seçenekler", L"GB", L"%{0}",
     L"DİSK", L"Disk", L"Sıcaklıkları çubukta göster", L"Disk", L"Okuma {0} · Yazma {1}", L"MB/sn", L"Sensörler", L"{0} RPM", L"Güncellemeler", L"Güncellemeleri şimdi denetle", L"Güncellemeleri otomatik yükle", L"{0} sürümünü yükle", L"{0} sürümünü indir", L"En son sürüme sahipsiniz ({0})", L"Güncellemeler denetleniyor…", L"{0} sürümü indiriliyor…", L"Güncelleme başarısız, daha sonra yeniden denenecek", L"{0} sürümü mevcut", L"Gelişmiş sensörleri etkinleştir (CPU, fanlar)…", L"Gelişmiş sensörler etkin", L"Gelişmiş sensörler: CPU ve fanlar (sağ tık)",
     L"Sıcaklık birimi", L"Otomatik (bölge)", L"Diskler", L"Her disk için bir sütun", L"Tüm diskler birlikte",
     L"Kasa", L"Pompa", L"Güç kaynağı", L"Fan {0}", L"fan durdu",
     L"AĞ", L"PİL", L"Ağ", L"İndirme {0} · Yükleme {1}", L"Ağ", L"Pil", L"KB/sn", L"GB/sn", L"Pil", L"Şarj oluyor", L"Pilde", L"Tamamen dolu", L"Prize takılı", L"{0} kaldı", L"Fanlar", L"Otomatik ({0})", L"Tüm görev çubuklarında göster", L"Güncel"},
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
     L"ГБ", L"{0}%",
     L"ДИСК", L"Диск", L"Показывать температуры на панели", L"Диск", L"Чтение {0} · Запись {1}", L"МБ/с", L"Датчики", L"{0} об/мин", L"Обновления", L"Проверить обновления сейчас", L"Устанавливать обновления автоматически", L"Установить версию {0}", L"Скачать версию {0}", L"У вас последняя версия ({0})", L"Проверка обновлений…", L"Загрузка версии {0}…", L"Не удалось обновить, повторим позже", L"Доступна версия {0}", L"Включить расширенные датчики (ЦП, вентиляторы)…", L"Расширенные датчики включены", L"Расширенные датчики: ЦП и вентиляторы (правый щелчок)",
     L"Единица температуры", L"Автоматически (регион)", L"Диски", L"Отдельный столбец для каждого диска", L"Все диски вместе",
     L"Корпус", L"Помпа", L"Блок питания", L"Вентилятор {0}", L"вентилятор остановлен",
     L"СЕТЬ", L"АКБ", L"Сеть", L"Приём {0} · Передача {1}", L"Сеть", L"Батарея", L"КБ/с", L"ГБ/с", L"Батарея", L"Заряжается", L"От батареи", L"Полностью заряжена", L"От сети", L"осталось {0}", L"Вентиляторы", L"Автоматически ({0})", L"Показывать на всех панелях задач", L"Актуальная версия"},
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
     L"ГБ", L"{0}%",
     L"ДИСК", L"Диск", L"Показувати температури на панелі", L"Диск", L"Читання {0} · Запис {1}", L"МБ/с", L"Датчики", L"{0} об/хв", L"Оновлення", L"Перевірити оновлення зараз", L"Встановлювати оновлення автоматично", L"Встановити версію {0}", L"Завантажити версію {0}", L"У вас остання версія ({0})", L"Перевірка оновлень…", L"Завантаження версії {0}…", L"Не вдалося оновити, повторимо пізніше", L"Доступна версія {0}", L"Увімкнути розширені датчики (ЦП, вентилятори)…", L"Розширені датчики увімкнено", L"Розширені датчики: ЦП і вентилятори (правий клік)",
     L"Одиниця температури", L"Автоматично (регіон)", L"Диски", L"Окремий стовпець для кожного диска", L"Усі диски разом",
     L"Корпус", L"Помпа", L"Блок живлення", L"Вентилятор {0}", L"вентилятор зупинено",
     L"МЕРЕЖА", L"АКБ", L"Мережа", L"Прийом {0} · Передавання {1}", L"Мережа", L"Акумулятор", L"КБ/с", L"ГБ/с", L"Акумулятор", L"Заряджається", L"Від акумулятора", L"Повністю заряджено", L"Від мережі", L"залишилось {0}", L"Вентилятори", L"Автоматично ({0})", L"Показувати на всіх панелях завдань", L"Актуальна версія"},
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
     L"右クリック: オプション", L"GB", L"{0}%",
     L"ディスク", L"ディスク", L"バーに温度を表示", L"ディスク", L"読み取り {0} · 書き込み {1}", L"MB/秒", L"センサー", L"{0} RPM", L"更新", L"今すぐ更新を確認", L"更新を自動的にインストール", L"バージョン {0} をインストール", L"バージョン {0} をダウンロード", L"最新バージョンです ({0})", L"更新を確認しています…", L"バージョン {0} をダウンロードしています…", L"更新に失敗しました。後で再試行します", L"バージョン {0} が利用可能", L"詳細センサーを有効にする (CPU、ファン)…", L"詳細センサーは有効です", L"詳細センサー: CPU とファン (右クリック)",
     L"温度の単位", L"自動 (地域)", L"ディスク", L"ディスクごとに 1 列", L"すべてのディスクをまとめる",
     L"ケース", L"ポンプ", L"電源", L"ファン {0}", L"ファン停止中",
     L"ネット", L"電池", L"ネットワーク", L"受信 {0} · 送信 {1}", L"ネットワーク", L"バッテリー", L"KB/秒", L"GB/秒", L"バッテリー", L"充電中", L"バッテリー駆動", L"充電完了", L"電源に接続", L"残り {0}", L"ファン", L"自動 ({0})", L"すべてのタスク バーに表示", L"最新です"},
    // 한국어
    {L"CPU", L"메모리(RAM)", L"GPU", L"GPU 메모리(VRAM)", L"업데이트 간격",
     L"{0}초", L"높은 부하 강조(주황/빨강)",
     L"클릭하면 작업 관리자 열기", L"Windows 시작 시 실행",
     L"언어", L"자동(시스템)", L"작업 관리자 열기",
     L"sys-bar 정보 {0}", L"sys-bar 종료",
     L"성능", L"CPU", L"메모리", L"GPU", L"GPU 메모리", L"{0} 중 · {1} 사용 중",
     L"최대 {0}", L"스레드 {0}개", L"최근 {0}초", L"최근 {0}분",
     L"클릭: 작업 관리자", L"오른쪽 클릭: 옵션", L"GB", L"{0}%",
     L"디스크", L"디스크", L"표시줄에 온도 표시", L"디스크", L"읽기 {0} · 쓰기 {1}", L"MB/s", L"센서", L"{0} RPM", L"업데이트", L"지금 업데이트 확인", L"업데이트 자동 설치", L"버전 {0} 설치", L"버전 {0} 다운로드", L"최신 버전입니다({0})", L"업데이트 확인 중…", L"버전 {0} 다운로드 중…", L"업데이트 실패, 나중에 다시 시도합니다", L"버전 {0} 사용 가능", L"고급 센서 사용(CPU, 팬)…", L"고급 센서 활성", L"고급 센서: CPU 및 팬(오른쪽 클릭)",
     L"온도 단위", L"자동(지역)", L"디스크", L"디스크마다 열 하나", L"모든 디스크 합치기",
     L"케이스", L"펌프", L"파워", L"팬 {0}", L"팬 정지",
     L"네트워크", L"배터리", L"네트워크", L"다운로드 {0} · 업로드 {1}", L"네트워크", L"배터리", L"KB/s", L"GB/s", L"배터리", L"충전 중", L"배터리 사용 중", L"완전히 충전됨", L"전원 연결됨", L"{0} 남음", L"팬", L"자동({0})", L"모든 작업 표시줄에 표시", L"최신 버전"},
    // 简体中文
    {L"CPU", L"内存 (RAM)", L"GPU", L"GPU 内存 (VRAM)", L"更新间隔", L"{0} 秒",
     L"突出显示高负载（琥珀色/红色）",
     L"单击时打开任务管理器", L"随 Windows 启动", L"语言",
     L"自动（系统）", L"打开任务管理器", L"关于 sys-bar {0}",
     L"退出 sys-bar",
     L"性能", L"CPU", L"内存", L"GPU", L"GPU 内存", L"共 {0} · 已用 {1}",
     L"最高 {0}", L"{0} 个线程", L"最近 {0} 秒", L"最近 {0} 分钟",
     L"单击：任务管理器", L"右键：选项", L"GB", L"{0}%",
     L"磁盘", L"磁盘", L"在任务栏中显示温度", L"磁盘", L"读取 {0} · 写入 {1}", L"MB/秒", L"传感器", L"{0} RPM", L"更新", L"立即检查更新", L"自动安装更新", L"安装版本 {0}", L"下载版本 {0}", L"已是最新版本（{0}）", L"正在检查更新…", L"正在下载版本 {0}…", L"更新失败，稍后重试", L"版本 {0} 可用", L"启用高级传感器（CPU、风扇）…", L"高级传感器已启用", L"高级传感器：CPU 和风扇（右键）",
     L"温度单位", L"自动（区域）", L"磁盘", L"每个磁盘一列", L"所有磁盘合并",
     L"机箱", L"水泵", L"电源", L"风扇 {0}", L"风扇已停转",
     L"网络", L"电池", L"网络", L"下载 {0} · 上传 {1}", L"网络", L"电池", L"KB/秒", L"GB/秒", L"电池", L"正在充电", L"使用电池", L"已充满", L"已接通电源", L"剩余 {0}", L"风扇", L"自动（{0}）", L"在所有任务栏上显示", L"已是最新"},
    // 繁體中文
    {L"CPU", L"記憶體 (RAM)", L"GPU", L"GPU 記憶體 (VRAM)", L"更新間隔",
     L"{0} 秒", L"醒目提示高負載（琥珀色/紅色）",
     L"按一下時開啟工作管理員", L"隨 Windows 啟動", L"語言",
     L"自動（系統）", L"開啟工作管理員", L"關於 sys-bar {0}",
     L"結束 sys-bar",
     L"效能", L"CPU", L"記憶體", L"GPU", L"GPU 記憶體",
     L"共 {0} · 已使用 {1}", L"最高 {0}", L"{0} 個執行緒",
     L"最近 {0} 秒", L"最近 {0} 分鐘", L"按一下：工作管理員",
     L"按右鍵：選項", L"GB", L"{0}%",
     L"磁碟", L"磁碟", L"在工作列中顯示溫度", L"磁碟", L"讀取 {0} · 寫入 {1}", L"MB/秒", L"感應器", L"{0} RPM", L"更新", L"立即檢查更新", L"自動安裝更新", L"安裝版本 {0}", L"下載版本 {0}", L"已是最新版本（{0}）", L"正在檢查更新…", L"正在下載版本 {0}…", L"更新失敗，稍後重試", L"版本 {0} 可用", L"啟用進階感應器（CPU、風扇）…", L"進階感應器已啟用", L"進階感應器：CPU 和風扇（按右鍵）",
     L"溫度單位", L"自動（地區）", L"磁碟", L"每個磁碟一欄", L"所有磁碟合併",
     L"機殼", L"水泵", L"電源", L"風扇 {0}", L"風扇已停止",
     L"網路", L"電池", L"網路", L"下載 {0} · 上傳 {1}", L"網路", L"電池", L"KB/秒", L"GB/秒", L"電池", L"充電中", L"使用電池", L"已充飽", L"已接上電源", L"剩餘 {0}", L"風扇", L"自動（{0}）", L"在所有工作列上顯示", L"已是最新"},
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
