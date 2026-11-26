#include "page.h"

#include "include/cef_version.h"

#include <array>
#include <string>

namespace aqua {
namespace {
using Action = SettingsAction;
constexpr std::string_view action_prefix = "aqua://settings/action/";
constexpr std::array options{
    std::pair{"restore-on", Action::restore_on}, std::pair{"restore-off", Action::restore_off},
    std::pair{"google", Action::google}, std::pair{"bing", Action::bing},
    std::pair{"duckduckgo", Action::duckduckgo}, std::pair{"zoom-90", Action::zoom_90},
    std::pair{"zoom-100", Action::zoom_100}, std::pair{"zoom-110", Action::zoom_110},
    std::pair{"zoom-125", Action::zoom_125}, std::pair{"extensions", Action::open_extensions},
    std::pair{"defaults", Action::reset_preferences}, std::pair{"factory", Action::factory_reset}
};

std::string Choice(std::string_view action, std::string_view label, bool selected = false,
                   bool destructive = false) {
    std::string html = "<a class=\"button";
    if (selected) html += " selected";
    if (destructive) html += " destructive";
    html += "\" href=\"";
    html += action_prefix;
    html += action;
    html += '"';
    if (selected) html += " aria-current=\"true\"";
    html += '>';
    html += label;
    html += "</a>";
    return html;
}

constexpr auto start = R"HTML(<!doctype html><html lang="ru"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Настройки</title>
<style>
:root{color-scheme:dark;font:15px 'Segoe UI',sans-serif;background:#0b1628;color:#e6f1ff}
*{box-sizing:border-box}body{margin:0;display:grid;grid-template-columns:224px minmax(0,1fr)}
aside{position:sticky;top:0;align-self:start;height:100vh;overflow:auto;padding:20px 12px;border-right:1px solid #2b3d52}
aside h1{margin:0 8px 16px;font-size:20px}aside a{display:block;padding:9px 10px;border-radius:8px;margin:3px 0}
aside a:hover,aside a:focus-visible{background:#153046;color:#7dd3fc}
a{color:inherit;text-decoration:none}main{padding:24px 32px 50px;max-width:900px}
section{margin-bottom:28px;scroll-margin-top:20px}
main section{display:none}main section:target,main:not(:has(section:target)) #general{display:block}
body:not(:has(section:target)) aside a[href="#general"],
body:has(#general:target) aside a[href="#general"],body:has(#appearance:target) aside a[href="#appearance"],
body:has(#extensions:target) aside a[href="#extensions"],body:has(#shortcuts:target) aside a[href="#shortcuts"],
body:has(#reset:target) aside a[href="#reset"],body:has(#about:target) aside a[href="#about"]{background:#153046;color:#7dd3fc}h2{font-size:21px;margin:0 0 12px}h3{font-size:15px;margin:0 0 8px}
.card{background:#14243a;border:1px solid #2b3d52;border-radius:8px;padding:14px;margin:8px 0}
p{color:#a8bacd;margin:6px 0 12px;line-height:1.45}.buttons{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}
.button{border:1px solid #2b3d52;border-radius:8px;padding:8px 12px;background:#0b1628}
.button:hover,.button:focus-visible{border-color:#7dd3fc}.selected{background:#153046;border-color:#00b8e6}
.destructive{border-color:#ef4444;color:#ffd7d7}.destructive:hover,.destructive:focus-visible{background:#4b202c}
kbd{border:1px solid #2b3d52;border-radius:4px;padding:2px 5px}
@media(max-width:630px){body{grid-template-columns:1fr}aside{position:static;height:auto;border-right:0;border-bottom:1px solid #2b3d52}aside nav{display:flex;flex-wrap:wrap}main{padding:18px}}
</style>
<aside><h1>Настройки</h1><nav aria-label="Разделы настроек">
<a href="#general">Общие</a><a href="#appearance">Внешний вид</a>
<a href="#extensions">Расширения</a><a href="#shortcuts">Горячие клавиши</a>
<a href="#reset">Сброс браузера</a><a href="#about">О браузере</a>
</nav></aside><main>
<section id="general"><h2>Общие</h2><div class="card"><h3>Запуск</h3>
<p>Восстановление вкладок после штатного закрытия браузера.</p><div class="buttons">
)HTML";
}

SettingsAction ParseSettingsAction(std::string_view url) {
    if (!url.starts_with(action_prefix)) return Action::unknown;
    const auto option = url.substr(action_prefix.size());
    for (const auto& [name, action] : options) {
        if (option == name) return action;
    }
    return Action::unknown;
}

std::string RenderSettings(const Preferences& settings) {
    std::string html(start);
    html += Choice("restore-off", "Открывать новую вкладку", !settings.restore_session);
    html += Choice("restore-on", "Восстанавливать сессию", settings.restore_session);
    html += R"HTML(</div></div><div class="card"><h3>Поисковая система</h3>
<p>Используется в адресной строке и на стартовой странице.</p><div class="buttons">)HTML";
    html += Choice("google", "Google", settings.search == SearchEngine::google);
    html += Choice("bing", "Bing", settings.search == SearchEngine::bing);
    html += Choice("duckduckgo", "DuckDuckGo", settings.search == SearchEngine::duckduckgo);
    html += R"HTML(</div></div></section>
<section id="appearance"><h2>Внешний вид</h2><div class="card"><h3>Aqua Deep Ocean</h3>
<p>Масштаб веб-страниц. Внутренние страницы сохраняют размер интерфейса.</p><div class="buttons">)HTML";
    html += Choice("zoom-90", "90%", settings.zoom == 90);
    html += Choice("zoom-100", "100%", settings.zoom == 100);
    html += Choice("zoom-110", "110%", settings.zoom == 110);
    html += Choice("zoom-125", "125%", settings.zoom == 125);
    html += R"HTML(</div></div></section><section id="extensions"><h2>Расширения</h2><div class="card">)HTML";
    html += Choice("extensions", "Открыть менеджер");
    html += R"HTML(</div></section>
<section id="shortcuts"><h2>Горячие клавиши</h2><div class="card">
<p><kbd>Ctrl+L</kbd> Адресная строка · <kbd>Ctrl+T</kbd> Новая вкладка · <kbd>Ctrl+W</kbd> Закрыть вкладку</p>
<p><kbd>Ctrl+Shift+T</kbd> Восстановить вкладку · <kbd>Ctrl+Shift+E</kbd> Расширения</p>
</div></section><section id="reset"><h2>Сброс браузера</h2><div class="card">
<h3>Обычный сброс</h3><p>Возвращает стандартный поиск, масштаб и настройки запуска. Данные сайтов сохраняются.</p>
)HTML";
    html += Choice("defaults", "Сбросить настройки");
    html += R"HTML(</div><div class="card"><h3>Полный заводской сброс</h3>
<p>Удаляет локальный профиль, данные сайтов, расширения и настройки. Браузер перезапустится с чистым профилем.</p>
)HTML";
    html += Choice("factory", "Удалить данные и перезапустить", false, true);
    html += R"HTML(</div></section><section id="about"><h2>О браузере</h2><div class="card">
<h3>Aqua Frame 1.0.0</h3><p>Beyond the ordinary.</p><p>CEF )HTML";
    html += CEF_VERSION;
    html += "</p></div></section></main></html>";
    return html;
}
}
