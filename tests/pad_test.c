/* Проверка раскладки геймпада: подключаем виртуальный Xbox-контроллер SDL
 * и смотрим, какие события MIDP выдаёт платформа. Запуск: SDL_VIDEODRIVER=dummy. */
#include "../src/platform.h"

#include <SDL.h>
#include <stdio.h>

#if !SDL_VERSION_ATLEAST(2, 24, 0)
/* виртуальный геймпад (SDL_JoystickAttachVirtualEx) появился в SDL 2.24 */
int main(void)
{
    printf("skip pad: SDL %d.%d.%d старее 2.24 — виртуального геймпада нет\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
           SDL_PATCHLEVEL);
    return 0;
}
#else

static int fails;

static void expect(const char *what, EventType type, int key)
{
    Event ev;
    int got = 0, i;

    for (i = 0; i < 50 && !got; i++) {
        got = platform_poll(&ev);
        if (!got)
            SDL_Delay(2);
    }
    if (!got) {
        printf("FAIL pad %s: событий нет\n", what);
        fails++;
    } else if (ev.type != type || (key && ev.key != key) || !ev.pad) {
        printf("FAIL pad %s: тип %d клавиша %d pad %d\n", what, ev.type, ev.key, ev.pad);
        fails++;
    } else {
        printf("ok   pad %s\n", what);
    }
}

static void expect_none(const char *what)
{
    Event ev;
    int i;

    for (i = 0; i < 10; i++) {
        if (platform_poll(&ev)) {
            printf("FAIL pad %s: лишнее событие %d/%d\n", what, ev.type, ev.key);
            fails++;
            return;
        }
        SDL_Delay(2);
    }
    printf("ok   pad %s\n", what);
}

int main(void)
{
    PlatformConfig cfg = { 240, 320, 0, SCALE_AUTO, 1, "pad-test", 0, 0 };
    SDL_VirtualJoystickDesc desc;
    SDL_Joystick *joy;
    int idx;

    if (platform_init(&cfg))
        return 1;
    SDL_zero(desc);
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.name = "Virtual Xbox Controller";
    idx = SDL_JoystickAttachVirtualEx(&desc);
    if (idx < 0 || !(joy = SDL_JoystickOpen(idx))) {
        printf("skip pad: виртуальный джойстик недоступен (%s)\n", SDL_GetError());
        return 0;
    }

    expect("подключение", EV_PAD_ADDED, 0);

    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_A, 1);
    expect("A -> огонь", EV_KEY_DOWN, KEY_FIRE);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_A, 0);
    expect("A отпущена", EV_KEY_UP, KEY_FIRE);

    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_B, 1);
    expect("B -> правая софт", EV_KEY_DOWN, KEY_SOFT_RIGHT);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_B, 0);
    expect("B отпущена", EV_KEY_UP, KEY_SOFT_RIGHT);

    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_START, 1);
    expect("Start -> левая софт", EV_KEY_DOWN, KEY_SOFT_LEFT);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_START, 0);
    expect("Start отпущена", EV_KEY_UP, KEY_SOFT_LEFT);

    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_DPAD_UP, 1);
    expect("крестовина вверх", EV_KEY_DOWN, KEY_UP);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_DPAD_UP, 0);
    expect("крестовина отпущена", EV_KEY_UP, KEY_UP);

    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_LEFTX, -30000);
    expect("стик влево", EV_KEY_DOWN, KEY_LEFT);
    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_LEFTX, -12000);
    expect_none("стик у порога не дребезжит");
    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_LEFTX, 0);
    expect("стик в центре", EV_KEY_UP, KEY_LEFT);

    /* в игре View открывает меню паузы, B его закрывает (кнопку Xbox виртуальный геймпад SDL
     * не присылает — она ведёт себя так же, как View) */
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_BACK, 1);
    expect_none("View в игре — без событий");
    if (!platform_paused()) {
        printf("FAIL pad View в игре -> меню паузы: меню закрыто\n");
        fails++;
    } else {
        printf("ok   pad View в игре -> меню паузы\n");
    }
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_BACK, 0);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_B, 1);
    expect_none("B в меню паузы — без событий");
    if (platform_paused()) {
        printf("FAIL pad B закрывает меню паузы\n");
        fails++;
    } else {
        printf("ok   pad B закрывает меню паузы\n");
    }
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_B, 0);
    expect("B отпущена после меню", EV_KEY_UP, KEY_SOFT_RIGHT);

    /* на полке (режим курсора) View — клавиша 0: сменить обложку */
    platform_cursor_mode(1);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_BACK, 1);
    expect("View на полке -> 0", EV_KEY_DOWN, KEY_NUM0);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_BACK, 0);
    expect("View на полке отпущена", EV_KEY_UP, KEY_NUM0);
    platform_cursor_mode(0);

    /* PS1: кнопки DualShock по месту, курки — L2/R2, View — Select, View+Start — меню паузы */
    platform_console_mode(CONSOLE_PSX);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_A, 1);
    expect("PS1: A -> ×", EV_KEY_DOWN, KEY_GAME_A);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_A, 0);
    expect("PS1: A отпущена", EV_KEY_UP, KEY_GAME_A);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_Y, 1);
    expect("PS1: Y -> △", EV_KEY_DOWN, KEY_GAME_Y);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_Y, 0);
    expect("PS1: Y отпущена", EV_KEY_UP, KEY_GAME_Y);
    {
        int hints = platform_hints();
        SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 1);
        expect("PS1: LB -> L1", EV_KEY_DOWN, KEY_GAME_L1);
        SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1);
        expect("PS1: RB -> R1", EV_KEY_DOWN, KEY_GAME_R1);
        SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0);
        expect("PS1: LB отпущена", EV_KEY_UP, KEY_GAME_L1);
        SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0);
        expect("PS1: RB отпущена", EV_KEY_UP, KEY_GAME_R1);
        if (platform_hints() != hints) {
            printf("FAIL pad PS1: L1+R1 не прячет подсказки\n");
            fails++;
        } else {
            printf("ok   pad PS1: L1+R1 не прячет подсказки\n");
        }
    }
    /* у виртуального геймпада курок идёт от -32768 (отпущен) до 32767; SDL переводит в 0..32767 */
    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 20000);
    expect("PS1: LT -> L2", EV_KEY_DOWN, KEY_GAME_L2);
    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_TRIGGERLEFT, -20000);
    expect_none("PS1: курок у порога не дребезжит");
    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
    expect("PS1: LT отпущен", EV_KEY_UP, KEY_GAME_L2);
    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 32767);
    expect("PS1: RT -> R2", EV_KEY_DOWN, KEY_GAME_R2);
    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
    expect("PS1: RT отпущен", EV_KEY_UP, KEY_GAME_R2);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_START, 1);
    expect("PS1: Start", EV_KEY_DOWN, KEY_GAME_START);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_START, 0);
    expect("PS1: Start отпущена", EV_KEY_UP, KEY_GAME_START);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_BACK, 1);
    expect("PS1: View -> Select", EV_KEY_DOWN, KEY_GAME_SELECT);
    if (platform_paused()) {
        printf("FAIL pad PS1: View без Start меню не открывает\n");
        fails++;
    }
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_START, 1);
    expect("PS1: View+Start отпускает Start", EV_KEY_UP, KEY_GAME_START);
    expect("PS1: View+Start отпускает Select", EV_KEY_UP, KEY_GAME_SELECT);
    if (!platform_paused()) {
        printf("FAIL pad PS1: View+Start -> меню паузы: меню закрыто\n");
        fails++;
    } else {
        printf("ok   pad PS1: View+Start -> меню паузы\n");
    }
    platform_console_mode(CONSOLE_OFF);

    SDL_JoystickClose(joy);
    platform_shutdown();
    printf(fails ? "провалено: %d\n" : "геймпад: все проверки прошли\n", fails);
    return fails ? 1 : 0;
}
#endif
