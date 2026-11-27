#include "page.h"

#include "include/cef_version.h"

#include <array>
#include <initializer_list>
#include <string>

namespace aqua {
namespace {
using Action = SettingsAction;
constexpr std::string_view action_prefix = "aqua://settings/action/";
constexpr std::array actions{
    std::pair{"restore-on", Action::restore_on}, std::pair{"restore-off", Action::restore_off},
    std::pair{"google", Action::google}, std::pair{"bing", Action::bing},
    std::pair{"duckduckgo", Action::duckduckgo}, std::pair{"zoom-90", Action::zoom_90},
    std::pair{"zoom-100", Action::zoom_100}, std::pair{"zoom-110", Action::zoom_110},
    std::pair{"zoom-125", Action::zoom_125}, std::pair{"extensions", Action::open_extensions},
    std::pair{"startup-new-tab", Action::startup_new_tab}, std::pair{"startup-settings", Action::startup_settings},
    std::pair{"theme-ocean", Action::theme_ocean}, std::pair{"theme-contrast", Action::theme_contrast},
    std::pair{"limit-restore-on", Action::limit_restore_on}, std::pair{"limit-restore-off", Action::limit_restore_off},
    std::pair{"warm-restore-on", Action::warm_restore_on}, std::pair{"warm-restore-off", Action::warm_restore_off},
    std::pair{"tab-unload-on", Action::tab_unload_on}, std::pair{"tab-unload-off", Action::tab_unload_off},
    std::pair{"high-gpu-on", Action::high_gpu_on}, std::pair{"high-gpu-off", Action::high_gpu_off},
    std::pair{"gpu-report", Action::open_gpu_report},
    std::pair{"downloads", Action::open_downloads},
    std::pair{"clear-history", Action::clear_history},
    std::pair{"animations-on", Action::animations_on}, std::pair{"animations-off", Action::animations_off},
    std::pair{"https-on", Action::https_on}, std::pair{"https-off", Action::https_off},
    std::pair{"defaults", Action::reset_preferences}, std::pair{"factory", Action::factory_reset}
};

struct Section {
    std::string_view id;
    std::string_view label;
    std::string_view icon;
};

constexpr std::array sections{
    Section{"general", "Общие", "M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zm0 5v4l3 2"},
    Section{"appearance", "Внешний вид", "M3 5h18v12H3zM12 17v4m-4 0h8"},
    Section{"performance", "Производительность", "M4 18a9 9 0 1 1 16 0zm8-3 4-5M7 10l1 1m8-4v1"},
    Section{"extensions", "Расширения", "M4 4h6V3a2 2 0 0 1 4 0v1h6v6h1a2 2 0 0 1 0 4h-1v6h-6v-1a2 2 0 0 0-4 0v1H4v-6h1a2 2 0 0 0 0-4H4z"},
    Section{"scripts", "Aqua Scripts", "M8 6l-6 6 6 6m8-12 6 6-6 6m-2-15-4 18"},
    Section{"security", "Безопасность", "M12 2 4 6v6c0 5 4 8 8 10 4-2 8-5 8-10V6z"},
    Section{"privacy", "Конфиденциальность", "M5 10h14v11H5zM8 10V7a4 4 0 0 1 8 0v3m-4 5v2"},
    Section{"search", "Поисковая система", "M10 3a7 7 0 1 0 0 14 7 7 0 0 0 0-14zm5 12 6 6"},
    Section{"downloads", "Загрузки", "M12 3v12m-5-5 5 5 5-5M4 16v5h16v-5"},
    Section{"shortcuts", "Горячие клавиши", "M2 5h20v14H2zM6 9h1m3 0h1m3 0h1m3 0h1M6 13h1m3 0h1m3 0h1m3 0h1M8 16h8"},
    Section{"reset", "Сброс браузера", "M20 11a8 8 0 1 0-2 6m2-13v7h-7"},
    Section{"updates", "Обновления", "M20 8a8 8 0 0 0-14-3L3 8m0-5v5h5m-4 8a8 8 0 0 0 14 3l3-3m0 5v-5h-5"},
    Section{"about", "О браузере", "M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zm0 8v6m0-10v1"}
};

constexpr auto css = R"HTML(
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Настройки</title><style>
:root{color-scheme:dark;--bg:#0b1628;--card:#14243a;--input:#101f31;--line:#2b3d52;--outline:#60778f;--text:#e6f1ff;--muted:#a8bacd;--accent:#00b8e6;--selection:#103c54;font:15px 'Segoe UI',sans-serif;background:var(--bg);color:var(--text)}
:root.contrast{--bg:#07101c;--card:#12243a;--input:#09182b;--line:#6687a7;--outline:#8caed1;--text:#fff;--muted:#d2e3f4;--accent:#55d8ff;--selection:#134c67}
*{box-sizing:border-box}body{margin:0;display:grid;grid-template-columns:224px minmax(0,1fr);min-height:100vh}
aside{position:sticky;top:0;align-self:start;height:100vh;overflow:auto;padding:14px 6px;border-right:1px solid var(--line)}
aside h1{margin:0 10px 16px;font-size:22px;font-weight:650}
nav a{display:flex;align-items:center;gap:13px;min-height:32px;padding:5px 11px;margin:2px 0;border-radius:8px;white-space:nowrap}
nav a svg{width:17px;height:17px;flex:none}
nav a:hover,nav a:focus-visible,.active{background:var(--selection);color:var(--text)}
a{color:inherit;text-decoration:none}main{padding:23px 30px 40px;max-width:1060px}
section{display:none}section:target,main:not(:has(section:target)) #general{display:block}
h2{font-size:28px;letter-spacing:.1px;margin:0 0 19px;font-weight:680}h3{font-size:17px;margin:0 0 9px;font-weight:650}
.card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:12px 16px;margin:0 0 14px}
p{color:var(--muted);margin:6px 0 12px;line-height:1.45}
p:last-child{margin-bottom:0}
details{box-sizing:border-box;border:1px solid var(--outline);border-radius:7px;background:var(--input)}
summary{cursor:pointer;list-style:none;display:flex;align-items:center;gap:8px;min-height:34px;padding:5px 12px}
summary::-webkit-details-marker{display:none}summary:after{content:'';width:9px;height:9px;margin-left:auto;border:solid var(--text);border-width:0 2px 2px 0;transform:translateY(-2px) rotate(45deg)}
details[open] summary:after{transform:translateY(2px) rotate(225deg)}
.options{border-top:1px solid var(--line);padding:4px}
.option{display:block;padding:6px 8px;border-radius:5px}.option:hover,.option[aria-current=true]{background:var(--selection)}
.google{font-size:19px;font-weight:800;background:conic-gradient(#4285f4 0 27%,#34a853 27% 53%,#fbbc05 53% 67%,#ea4335 67% 87%,#4285f4 87%);background-clip:text;color:transparent}
.switch-row{display:flex;min-height:37px;align-items:center;justify-content:space-between;gap:12px;margin-top:10px;padding-top:8px;border-top:1px solid var(--line)}
.switch{position:relative;flex:none;width:43px;height:23px;border-radius:20px;background:#43586c}
.switch:after{content:'';position:absolute;width:17px;height:17px;top:3px;left:3px;border-radius:50%;background:#e6f1ff}
.switch[aria-checked=true]{background:var(--accent)}.switch[aria-checked=true]:after{left:23px}
.button{display:inline-block;border:1px solid var(--line);border-radius:8px;padding:8px 12px;background:var(--input)}
.button:hover,.button:focus-visible{border-color:var(--accent)}.destructive{border-color:#ef4444;color:#ffd7d7}
.destructive:hover,.destructive:focus-visible{background:#4b202c}
kbd{border:1px solid var(--line);border-radius:4px;padding:2px 5px}
a:focus-visible,summary:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
@media(max-width:640px){body{grid-template-columns:1fr}aside{position:static;height:auto;border-right:0;border-bottom:1px solid var(--line)}aside nav{display:flex;flex-wrap:wrap}main{padding:18px}}
)HTML";

std::string Link(std::string_view action, std::string_view label,
                 std::string_view attributes = "class=\"button\"") {
    return "<a " + std::string(attributes) + " href=\"" + std::string(action_prefix) +
        std::string(action) + "\">" + std::string(label) + "</a>";
}

using Choice = std::pair<std::string_view, std::string_view>;

std::string Dropdown(std::string_view selected, std::initializer_list<Choice> options, bool google = false) {
    std::string html = "<details><summary>";
    if (google) html += R"HTML(<span class="google" aria-hidden="true">G</span>)HTML";
    html += selected;
    html += "</summary><div class=\"options\">";
    for (const auto& [action, label] : options) {
        html += Link(action, label, selected == label ? "class=\"option\" aria-current=\"true\"" : "class=\"option\"");
    }
    html += "</div></details>";
    return html;
}

std::string Switch(std::string_view label, std::string_view on, std::string_view off, bool enabled) {
    return "<div class=\"switch-row\"><span>" + std::string(label) + "</span>" +
        Link(enabled ? off : on, "", "class=\"switch\" role=\"switch\" aria-label=\"" +
            std::string(label) + "\" aria-checked=\"" + (enabled ? "true" : "false") + "\"") + "</div>";
}

std::string SearchDropdown(const Preferences& preferences) {
    return Dropdown(SearchName(preferences.search), {
        {"google", "Google"}, {"bing", "Bing"}, {"duckduckgo", "DuckDuckGo"}
    }, preferences.search == SearchEngine::google);
}

std::string ThemeDropdown(const Preferences& preferences) {
    constexpr auto ocean = "Aqua Deep Ocean";
    constexpr auto contrast = "Aqua Deep Ocean · контраст";
    return Dropdown(preferences.theme == Theme::ocean ? ocean : contrast,
                    {{"theme-ocean", ocean}, {"theme-contrast", contrast}});
}
}

SettingsAction ParseSettingsAction(std::string_view url) {
    if (!url.starts_with(action_prefix)) return Action::unknown;
    const auto option = url.substr(action_prefix.size());
    for (const auto& [name, action] : actions) {
        if (option == name) return action;
    }
    return Action::unknown;
}

std::string RenderSettings(const Preferences& settings, const std::vector<Script>& scripts) {
    std::string html = settings.theme == Theme::contrast
        ? "<!doctype html><html lang=\"ru\" class=\"contrast\">"
        : "<!doctype html><html lang=\"ru\">";
    html += css;
    for (const auto& section : sections) {
        html += "body:has(#" + std::string(section.id) + ":target) nav a[href=\"#" +
            std::string(section.id) + "\"]{background:var(--selection)}";
    }
    html += R"HTML(body:not(:has(section:target)) nav a[href="#general"]{background:var(--selection)}
</style><aside><h1>Настройки</h1><nav aria-label="Разделы настроек">)HTML";
    for (const auto& section : sections) {
        html += "<a href=\"#" + std::string(section.id) + "\"><svg aria-hidden=\"true\" viewBox=\"0 0 24 24\" "
            "fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"";
        html += section.icon;
        html += "\"/></svg>";
        html += section.label;
        html += "</a>";
    }

    html += R"HTML(</nav></aside><main><section id="general"><h2>Общие</h2><div class="card"><h3>Запуск</h3>)HTML";
    constexpr auto new_tab = "Открывать новую вкладку";
    constexpr auto settings_tab = "Открывать настройки";
    html += Dropdown(settings.startup_page == StartupPage::settings ? settings_tab : new_tab,
                    {{"startup-new-tab", new_tab}, {"startup-settings", settings_tab}});
    html += Switch("Восстанавливать предыдущую сессию", "restore-on", "restore-off", settings.restore_session);
    html += R"HTML(</div><div class="card"><h3>Поисковая система</h3>)HTML";
    html += SearchDropdown(settings);
    html += R"HTML(</div><div class="card"><h3>Внешний вид</h3>)HTML";
    html += ThemeDropdown(settings);
    html += R"HTML(</div></section>
<section id="appearance"><h2>Внешний вид</h2><div class="card"><h3>Тема</h3>)HTML";
    html += ThemeDropdown(settings);
    html += R"HTML(</div><div class="card"><h3>Масштаб веб-страниц</h3>)HTML";
    html += Dropdown(std::to_string(settings.zoom) + "%", {
        {"zoom-90", "90%"}, {"zoom-100", "100%"}, {"zoom-110", "110%"}, {"zoom-125", "125%"}
    });
    html += Switch("Короткие анимации главной страницы", "animations-on", "animations-off", settings.animations);
    html += R"HTML(</div></section>
<section id="performance"><h2>Производительность</h2><div class="card"><h3>Восстановление вкладок</h3>
<p>При восстановлении сессии можно ограничить число вкладок, загружаемых при запуске.</p>)HTML";
    html += Switch("Открывать только восемь последних вкладок", "limit-restore-on", "limit-restore-off", settings.limit_restored_tabs);
    html += Switch("Заранее загружать одну вкладку после восстановления", "warm-restore-on", "warm-restore-off", settings.warm_restored_tab);
    html += R"HTML(<p>После открытия активной вкладки браузер может заранее загрузить одну соседнюю. Остальные загрузятся при выборе.</p></div>
<div class="card"><h3>Оперативная память</h3>
<p>Выгруженная вкладка остаётся в списке и открывается заново при выборе. Используйте команду из меню фоновой вкладки, когда на ней нет важных задач или несохранённых данных.</p>)HTML";
    html += Switch("Разрешить ручную выгрузку фоновых вкладок", "tab-unload-on", "tab-unload-off", settings.allow_tab_unload);
    html += R"HTML(</div><div class="card"><h3>Графика веб-страниц</h3>
<p>На компьютерах с несколькими видеокартами Chromium может предпочесть более мощную. Для применения требуется перезапуск Aqua Frame.</p>)HTML";
    html += Switch("Предпочитать высокопроизводительную видеокарту", "high-gpu-on", "high-gpu-off",
                   settings.prefer_high_performance_gpu);
    html += "<p>" + Link("gpu-report", "Открыть отчёт об аппаратном ускорении") + "</p>";
    html += R"HTML(</div></section><section id="extensions"><h2>Расширения</h2><div class="card">)HTML";
    html += Link("extensions", "Открыть менеджер");
    html += R"HTML(</div></section>
<section id="scripts"><h2>Aqua Scripts</h2><div class="card"><h3>Импорт JavaScript</h3>
<p>Откройте сайт, для которого нужен скрипт. Затем выберите файл .js или .user.js либо перетащите его из Проводника на верхнюю панель Aqua Frame. Скрипт запустится при следующей загрузке разрешённого сайта.</p>)HTML";
    html += Link("script-import", "Выбрать файл JS");
    html += R"HTML(<p>Можно перетащить .js или .user.js из Проводника на эту страницу.</p>
<p id="script-drop-status" role="status"></p><script src="/script-drop.js" defer></script>)HTML";
    html += "</div>";
    if (scripts.empty()) html += "<div class=\"card\"><p>Скриптов пока нет.</p></div>";
    for (const auto& script : scripts) {
        html += "<div class=\"card\"><h3>" + HtmlEscape(script.name) + "</h3><p>" +
            HtmlEscape(script.origin) + (script.enabled ? " · Включён" : " · Выключен") + "</p>";
        html += Link((script.enabled ? "script-disable/" : "script-enable/") + script.id,
                     script.enabled ? "Выключить" : "Включить");
        html += " ";
        html += Link("script-remove/" + script.id, "Удалить");
        html += "</div>";
    }
    html += R"HTML(</section><section id="security"><h2>Безопасность</h2><div class="card"><h3>Chromium Sandbox</h3><p>Aqua Frame запускается с включённой песочницей Chromium и использует проверку сертификатов Chromium.</p>)HTML";
    html += Switch("HTTPS-Only для переходов в основной вкладке", "https-on", "https-off", settings.https_only);
    html += R"HTML(<p>В этом режиме Aqua Frame открывает защищённую версию HTTP-адреса и блокирует переходы обратно на HTTP.</p></div></section>
<section id="privacy"><h2>Конфиденциальность</h2><div class="card"><h3>Локальные данные</h3><p>Ctrl+Shift+N открывает вкладку инкогнито с отдельными временными cookies и хранилищем.</p><a class="button" href="#reset">Открыть сброс браузера</a> )HTML";
    html += Link("clear-history", "Очистить историю", "class=\"button destructive\"");
    html += R"HTML(</div></section>
<section id="search"><h2>Поисковая система</h2><div class="card"><h3>Поиск по умолчанию</h3><p>Используется в адресной строке и на стартовой странице.</p>)HTML";
    html += SearchDropdown(settings);
    html += R"HTML(</div></section><section id="downloads"><h2>Загрузки</h2><div class="card">)HTML";
    html += Link("downloads", "Открыть папку загрузок Windows");
    html += R"HTML( <a class="button" href="aqua://library/downloads">Открыть список загрузок</a>)HTML";
    html += R"HTML(</div></section><section id="shortcuts"><h2>Горячие клавиши</h2><div class="card">
<p><kbd>Ctrl+L</kbd> Адресная строка · <kbd>Ctrl+T</kbd> Новая вкладка · <kbd>Ctrl+W</kbd> Закрыть вкладку</p>
<p><kbd>Ctrl+Shift+T</kbd> Восстановить вкладку · <kbd>Ctrl+Shift+E</kbd> Расширения</p>
<p><kbd>Ctrl+H</kbd> История · <kbd>Ctrl+J</kbd> Загрузки · <kbd>Ctrl+D</kbd> Закладка · <kbd>Ctrl+Shift+B</kbd> Панель закладок</p>
<p><kbd>Ctrl+Shift+N</kbd> Инкогнито · <kbd>F11</kbd> Полный экран · <kbd>F12</kbd> Инструменты разработчика</p>
</div></section><section id="reset"><h2>Сброс браузера</h2><div class="card">
<h3>Обычный сброс</h3><p>Возвращает стандартные настройки. Данные сайтов сохраняются.</p>
)HTML";
    html += Link("defaults", "Сбросить настройки");
    html += R"HTML(</div><div class="card"><h3>Полный заводской сброс</h3>
<p>Удаляет локальный профиль, данные сайтов, расширения и настройки. Браузер перезапустится с чистым профилем.</p>
)HTML";
    html += Link("factory", "Удалить данные и перезапустить", "class=\"button destructive\"");
    html += R"HTML(</div></section>
<section id="updates"><h2>Обновления</h2><div class="card"><h3>Сборки для Windows</h3>
<p>Обновление устанавливается вручную. Здесь публикуются проверенные сборки Windows.</p>
<a class="button" href="https://github.com/hamsterbusines337-create/Aqua-Frame/actions/workflows/windows.yml">Открыть список сборок</a>
</div></section><section id="about"><h2>О браузере</h2><div class="card">
<h3>Aqua Frame 1.0.0</h3><p>Beyond the ordinary.</p><p>CEF )HTML";
    html += CEF_VERSION;
    html += "</p></div></section></main></html>";
    return html;
}
}
