//! System-tray icon and menu. This is the only way to control (and quit) the
//! overlay: the overlay window itself ignores all input.

use crate::config::Config;
use crate::shaders::BUNDLED;
use anyhow::Result;
use tray_icon::menu::{
    CheckMenuItem, Menu, MenuEvent, MenuId, MenuItem, PredefinedMenuItem, Submenu,
};
use tray_icon::{Icon, TrayIcon, TrayIconBuilder};

#[derive(Debug, Clone, PartialEq)]
pub enum Command {
    ToggleEnabled,
    SetPreset(String),
    SetLines(u32),
    SetMonitor(usize),
    Reload,
    OpenConfig,
    Quit,
}

/// Scanline choices; 0 = auto.
pub const LINES: &[u32] = &[0, 180, 200, 224, 240, 288, 360, 480];

pub struct Tray {
    icon: TrayIcon,
    stats: MenuItem,
    enabled: CheckMenuItem,
    presets: Vec<(CheckMenuItem, String)>,
    lines: Vec<(CheckMenuItem, u32)>,
    monitors: Vec<(CheckMenuItem, usize)>,
    reload: MenuId,
    open_config: MenuId,
    quit: MenuId,
}

impl Tray {
    /// `monitor_names[i]` is shown for config value `i + 1`; 0 is "primary".
    pub fn new(cfg: &Config, monitor_names: &[String]) -> Result<Tray> {
        let menu = Menu::new();

        let title = MenuItem::new("CRT Overlay", false, None);
        let stats = MenuItem::new("Нагрузка: —", false, None);
        let enabled = CheckMenuItem::new("Шейдер включён", true, cfg.enabled, None);

        let preset_menu = Submenu::new("Режим / пресет", true);
        let mut presets = Vec::new();
        for (name, label) in BUNDLED {
            let item = CheckMenuItem::new(*label, true, cfg.preset == *name, None);
            preset_menu.append(&item)?;
            presets.push((item, name.to_string()));
        }
        if !BUNDLED.iter().any(|(n, _)| *n == cfg.preset) {
            let item =
                CheckMenuItem::new(format!("Свой: {}", short(&cfg.preset)), true, true, None);
            preset_menu.append(&item)?;
            presets.push((item, cfg.preset.clone()));
        }

        let lines_menu = Submenu::new("Строк развёртки", true);
        let mut lines = Vec::new();
        for &value in LINES {
            let label = if value == 0 {
                "Авто (240 на 1080p)".to_string()
            } else {
                value.to_string()
            };
            let item = CheckMenuItem::new(label, true, cfg.lines == value, None);
            lines_menu.append(&item)?;
            lines.push((item, value));
        }

        let monitor_menu = Submenu::new("Монитор", monitor_names.len() > 1);
        let mut monitors = Vec::new();
        let primary = CheckMenuItem::new("Основной", true, cfg.monitor == 0, None);
        monitor_menu.append(&primary)?;
        monitors.push((primary, 0));
        for (i, name) in monitor_names.iter().enumerate() {
            let item = CheckMenuItem::new(
                format!("{}: {name}", i + 1),
                true,
                cfg.monitor == i + 1,
                None,
            );
            monitor_menu.append(&item)?;
            monitors.push((item, i + 1));
        }

        let reload = MenuItem::new("Перезагрузить шейдер и настройки", true, None);
        let open_config = MenuItem::new("Открыть файл настроек", true, None);
        let quit = MenuItem::new("Выход", true, None);

        menu.append_items(&[
            &title,
            &stats,
            &PredefinedMenuItem::separator(),
            &enabled,
            &preset_menu,
            &lines_menu,
            &monitor_menu,
            &PredefinedMenuItem::separator(),
            &reload,
            &open_config,
            &PredefinedMenuItem::separator(),
            &quit,
        ])?;

        let icon = TrayIconBuilder::new()
            .with_menu(Box::new(menu))
            .with_tooltip("CRT Overlay")
            .with_title("CRT Overlay")
            .with_icon(make_icon())
            .with_menu_on_left_click(true)
            .build()?;

        Ok(Tray {
            icon,
            stats,
            enabled,
            presets,
            lines,
            monitors,
            reload: reload.id().clone(),
            open_config: open_config.id().clone(),
            quit: quit.id().clone(),
        })
    }

    /// Translate a menu click into a command and keep the check marks consistent
    /// (submenus behave like radio groups).
    pub fn handle(&self, event: &MenuEvent) -> Option<Command> {
        let id = &event.id;
        if id == self.enabled.id() {
            return Some(Command::ToggleEnabled);
        }
        if let Some(name) = radio(&self.presets, id) {
            return Some(Command::SetPreset(name));
        }
        if let Some(lines) = radio(&self.lines, id) {
            return Some(Command::SetLines(lines));
        }
        if let Some(monitor) = radio(&self.monitors, id) {
            return Some(Command::SetMonitor(monitor));
        }
        if id == &self.reload {
            return Some(Command::Reload);
        }
        if id == &self.open_config {
            return Some(Command::OpenConfig);
        }
        if id == &self.quit {
            return Some(Command::Quit);
        }
        None
    }

    pub fn set_enabled(&self, on: bool) {
        self.enabled.set_checked(on);
        if !on {
            self.set_stats("Нагрузка: выключено");
        }
    }

    /// Show the current load in the menu and the icon tooltip.
    pub fn set_stats(&self, text: &str) {
        self.stats.set_text(text);
        let _ = self.icon.set_tooltip(Some(format!("CRT Overlay\n{text}")));
    }
}

fn radio<T: Clone>(items: &[(CheckMenuItem, T)], id: &MenuId) -> Option<T> {
    let hit = items.iter().position(|(item, _)| item.id() == id)?;
    for (i, (item, _)) in items.iter().enumerate() {
        item.set_checked(i == hit);
    }
    Some(items[hit].1.clone())
}

fn short(path: &str) -> String {
    std::path::Path::new(path)
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_else(|| path.to_string())
}

/// A tiny CRT monitor drawn in code: grey bezel, green screen with scanlines.
fn make_icon() -> Icon {
    const N: u32 = 32;
    let mut rgba = vec![0u8; (N * N * 4) as usize];
    for y in 0..N {
        for x in 0..N {
            let i = ((y * N + x) * 4) as usize;
            let px: [u8; 4] = if (3..=28).contains(&x) && (4..=24).contains(&y) {
                if (6..=25).contains(&x) && (7..=21).contains(&y) {
                    if y % 2 == 0 {
                        [40, 200, 90, 255]
                    } else {
                        [10, 70, 30, 255]
                    }
                } else {
                    [70, 70, 78, 255]
                }
            } else if (12..=19).contains(&x) && (25..=27).contains(&y) {
                [70, 70, 78, 255]
            } else {
                [0, 0, 0, 0]
            };
            rgba[i..i + 4].copy_from_slice(&px);
        }
    }
    Icon::from_rgba(rgba, N, N).expect("valid icon")
}

/// Forward menu clicks into the winit event loop (they arrive on whatever
/// thread the tray backend uses).
pub fn forward_events(proxy: winit::event_loop::EventLoopProxy<crate::UserEvent>) {
    MenuEvent::set_event_handler(Some(move |e: MenuEvent| {
        let _ = proxy.send_event(crate::UserEvent::Menu(e));
    }));
}
