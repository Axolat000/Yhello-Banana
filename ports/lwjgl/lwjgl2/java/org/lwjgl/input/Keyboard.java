package org.lwjgl.input;

import java.util.ArrayDeque;

import org.lwjgl.LWJGLException;

/**
 * LWJGL 2's Keyboard over GLFW: its key codes (the DirectInput ones), names
 * and event queue. Display feeds it from GLFW's key and character callbacks
 * (during Display.update / processMessages); a character is put on the key
 * press it came with, as LWJGL 2 delivered them (Minecraft 1.7.10's text
 * fields read characters from key presses only), and comes as an event of
 * its own (KEY_NONE, pressed) when there is no such press.
 */
public final class Keyboard {
    private Keyboard() {}

    public static final int CHAR_NONE = 0;
    public static final int KEYBOARD_SIZE = 256;
    public static final int EVENT_SIZE = 18;
    public static final int KEY_NONE = 0;
    public static final int KEY_ESCAPE = 1;
    public static final int KEY_1 = 2;
    public static final int KEY_2 = 3;
    public static final int KEY_3 = 4;
    public static final int KEY_4 = 5;
    public static final int KEY_5 = 6;
    public static final int KEY_6 = 7;
    public static final int KEY_7 = 8;
    public static final int KEY_8 = 9;
    public static final int KEY_9 = 10;
    public static final int KEY_0 = 11;
    public static final int KEY_MINUS = 12;
    public static final int KEY_EQUALS = 13;
    public static final int KEY_BACK = 14;
    public static final int KEY_TAB = 15;
    public static final int KEY_Q = 16;
    public static final int KEY_W = 17;
    public static final int KEY_E = 18;
    public static final int KEY_R = 19;
    public static final int KEY_T = 20;
    public static final int KEY_Y = 21;
    public static final int KEY_U = 22;
    public static final int KEY_I = 23;
    public static final int KEY_O = 24;
    public static final int KEY_P = 25;
    public static final int KEY_LBRACKET = 26;
    public static final int KEY_RBRACKET = 27;
    public static final int KEY_RETURN = 28;
    public static final int KEY_LCONTROL = 29;
    public static final int KEY_A = 30;
    public static final int KEY_S = 31;
    public static final int KEY_D = 32;
    public static final int KEY_F = 33;
    public static final int KEY_G = 34;
    public static final int KEY_H = 35;
    public static final int KEY_J = 36;
    public static final int KEY_K = 37;
    public static final int KEY_L = 38;
    public static final int KEY_SEMICOLON = 39;
    public static final int KEY_APOSTROPHE = 40;
    public static final int KEY_GRAVE = 41;
    public static final int KEY_LSHIFT = 42;
    public static final int KEY_BACKSLASH = 43;
    public static final int KEY_Z = 44;
    public static final int KEY_X = 45;
    public static final int KEY_C = 46;
    public static final int KEY_V = 47;
    public static final int KEY_B = 48;
    public static final int KEY_N = 49;
    public static final int KEY_M = 50;
    public static final int KEY_COMMA = 51;
    public static final int KEY_PERIOD = 52;
    public static final int KEY_SLASH = 53;
    public static final int KEY_RSHIFT = 54;
    public static final int KEY_MULTIPLY = 55;
    public static final int KEY_LMENU = 56;
    public static final int KEY_SPACE = 57;
    public static final int KEY_CAPITAL = 58;
    public static final int KEY_F1 = 59;
    public static final int KEY_F2 = 60;
    public static final int KEY_F3 = 61;
    public static final int KEY_F4 = 62;
    public static final int KEY_F5 = 63;
    public static final int KEY_F6 = 64;
    public static final int KEY_F7 = 65;
    public static final int KEY_F8 = 66;
    public static final int KEY_F9 = 67;
    public static final int KEY_F10 = 68;
    public static final int KEY_NUMLOCK = 69;
    public static final int KEY_SCROLL = 70;
    public static final int KEY_NUMPAD7 = 71;
    public static final int KEY_NUMPAD8 = 72;
    public static final int KEY_NUMPAD9 = 73;
    public static final int KEY_SUBTRACT = 74;
    public static final int KEY_NUMPAD4 = 75;
    public static final int KEY_NUMPAD5 = 76;
    public static final int KEY_NUMPAD6 = 77;
    public static final int KEY_ADD = 78;
    public static final int KEY_NUMPAD1 = 79;
    public static final int KEY_NUMPAD2 = 80;
    public static final int KEY_NUMPAD3 = 81;
    public static final int KEY_NUMPAD0 = 82;
    public static final int KEY_DECIMAL = 83;
    public static final int KEY_F11 = 87;
    public static final int KEY_F12 = 88;
    public static final int KEY_F13 = 100;
    public static final int KEY_F14 = 101;
    public static final int KEY_F15 = 102;
    public static final int KEY_F16 = 103;
    public static final int KEY_F17 = 104;
    public static final int KEY_F18 = 105;
    public static final int KEY_KANA = 112;
    public static final int KEY_F19 = 113;
    public static final int KEY_CONVERT = 121;
    public static final int KEY_NOCONVERT = 123;
    public static final int KEY_YEN = 125;
    public static final int KEY_NUMPADEQUALS = 141;
    public static final int KEY_CIRCUMFLEX = 144;
    public static final int KEY_AT = 145;
    public static final int KEY_COLON = 146;
    public static final int KEY_UNDERLINE = 147;
    public static final int KEY_KANJI = 148;
    public static final int KEY_STOP = 149;
    public static final int KEY_AX = 150;
    public static final int KEY_UNLABELED = 151;
    public static final int KEY_NUMPADENTER = 156;
    public static final int KEY_RCONTROL = 157;
    public static final int KEY_SECTION = 167;
    public static final int KEY_NUMPADCOMMA = 179;
    public static final int KEY_DIVIDE = 181;
    public static final int KEY_SYSRQ = 183;
    public static final int KEY_RMENU = 184;
    public static final int KEY_FUNCTION = 196;
    public static final int KEY_PAUSE = 197;
    public static final int KEY_HOME = 199;
    public static final int KEY_UP = 200;
    public static final int KEY_PRIOR = 201;
    public static final int KEY_LEFT = 203;
    public static final int KEY_RIGHT = 205;
    public static final int KEY_END = 207;
    public static final int KEY_DOWN = 208;
    public static final int KEY_NEXT = 209;
    public static final int KEY_INSERT = 210;
    public static final int KEY_DELETE = 211;
    public static final int KEY_CLEAR = 218;
    public static final int KEY_LMETA = 219;
    public static final int KEY_LWIN = 219;
    public static final int KEY_RMETA = 220;
    public static final int KEY_RWIN = 220;
    public static final int KEY_APPS = 221;
    public static final int KEY_POWER = 222;
    public static final int KEY_SLEEP = 223;

    private static final String[] NAMES = new String[KEYBOARD_SIZE];
    static {
        for (java.lang.reflect.Field f : Keyboard.class.getFields()) {
            if (!f.getName().startsWith("KEY_") || f.getType() != int.class) continue;
            try {
                int k = f.getInt(null);
                if (k >= 0 && k < KEYBOARD_SIZE && NAMES[k] == null) NAMES[k] = f.getName().substring(4);
            } catch (IllegalAccessException e) { /* (public) */ }
        }
    }

    /* GLFW key -> LWJGL 2 key */
    private static final int[] FROM_GLFW = new int[349];
    private static void map(int glfw, int key) { FROM_GLFW[glfw] = key; }
    static {
        map(32, KEY_SPACE); map(39, KEY_APOSTROPHE); map(44, KEY_COMMA); map(45, KEY_MINUS); map(46, KEY_PERIOD);
        map(47, KEY_SLASH); map(59, KEY_SEMICOLON); map(61, KEY_EQUALS); map(91, KEY_LBRACKET); map(92, KEY_BACKSLASH);
        map(93, KEY_RBRACKET); map(96, KEY_GRAVE); map(161, KEY_BACKSLASH); map(162, KEY_BACKSLASH);
        int[] digits = { KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };
        for (int i = 0; i < 10; i++) map(48 + i, digits[i]);
        int[] letters = { KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
                          KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z };
        for (int i = 0; i < 26; i++) map(65 + i, letters[i]);
        map(256, KEY_ESCAPE); map(257, KEY_RETURN); map(258, KEY_TAB); map(259, KEY_BACK); map(260, KEY_INSERT);
        map(261, KEY_DELETE); map(262, KEY_RIGHT); map(263, KEY_LEFT); map(264, KEY_DOWN); map(265, KEY_UP);
        map(266, KEY_PRIOR); map(267, KEY_NEXT); map(268, KEY_HOME); map(269, KEY_END);
        map(280, KEY_CAPITAL); map(281, KEY_SCROLL); map(282, KEY_NUMLOCK); map(283, KEY_SYSRQ); map(284, KEY_PAUSE);
        int[] fkeys = { KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
                        KEY_F13, KEY_F14, KEY_F15, KEY_F16, KEY_F17, KEY_F18, KEY_F19 };
        for (int i = 0; i < fkeys.length; i++) map(290 + i, fkeys[i]);
        int[] pad = { KEY_NUMPAD0, KEY_NUMPAD1, KEY_NUMPAD2, KEY_NUMPAD3, KEY_NUMPAD4, KEY_NUMPAD5, KEY_NUMPAD6,
                      KEY_NUMPAD7, KEY_NUMPAD8, KEY_NUMPAD9 };
        for (int i = 0; i < 10; i++) map(320 + i, pad[i]);
        map(330, KEY_DECIMAL); map(331, KEY_DIVIDE); map(332, KEY_MULTIPLY); map(333, KEY_SUBTRACT); map(334, KEY_ADD);
        map(335, KEY_NUMPADENTER); map(336, KEY_NUMPADEQUALS);
        map(340, KEY_LSHIFT); map(341, KEY_LCONTROL); map(342, KEY_LMENU); map(343, KEY_LMETA);
        map(344, KEY_RSHIFT); map(345, KEY_RCONTROL); map(346, KEY_RMENU); map(347, KEY_RMETA); map(348, KEY_APPS);
    }

    /* an event: key, pressed, character, repeat, when */
    private static final class Ev {
        final int key;
        final boolean down, repeat;
        final long nanos = System.nanoTime();
        char ch;
        Ev(int key, boolean down, char ch, boolean repeat) { this.key = key; this.down = down; this.ch = ch; this.repeat = repeat; }
    }

    private static final ArrayDeque<Ev> queue = new ArrayDeque<>();
    private static final boolean[] down = new boolean[KEYBOARD_SIZE];
    private static Ev current;
    private static boolean created, repeats;

    /* ── from Display (GLFW's callbacks) ── */
    static void glfwKey(int glfwKey, int action) {
        int k = glfwKey >= 0 && glfwKey < FROM_GLFW.length ? FROM_GLFW[glfwKey] : KEY_NONE;
        if (action == 2) {                                         /* GLFW_REPEAT */
            if (repeats && k != KEY_NONE) queue.add(new Ev(k, true, (char) 0, true));
            return;
        }
        boolean press = action == 1;
        if (k != KEY_NONE) down[k] = press;
        queue.add(new Ev(k, press, (char) 0, false));
    }

    static void glfwChar(int codepoint) {
        if (codepoint <= 0 || codepoint > 0xFFFF) return;
        Ev last = queue.peekLast();
        if (last != null && last.down && last.ch == 0 && last.key != KEY_NONE) last.ch = (char) codepoint;
        else queue.add(new Ev(KEY_NONE, true, (char) codepoint, false));
    }

    static void reset() { queue.clear(); java.util.Arrays.fill(down, false); current = null; }

    /* ── the API ── */
    public static void create() throws LWJGLException { created = true; }
    public static boolean isCreated() { return created; }
    public static void destroy() { created = false; reset(); }
    public static void poll() {}
    public static boolean isKeyDown(int key) { return key >= 0 && key < KEYBOARD_SIZE && down[key]; }
    public static synchronized String getKeyName(int key) { return key >= 0 && key < KEYBOARD_SIZE ? NAMES[key] : null; }
    public static synchronized int getKeyIndex(String name) {
        for (int i = 0; i < KEYBOARD_SIZE; i++) if (name.equals(NAMES[i])) return i;
        return KEY_NONE;
    }
    public static int getNumKeyboardEvents() { return queue.size(); }
    public static boolean next() { current = queue.poll(); return current != null; }
    public static void enableRepeatEvents(boolean on) { repeats = on; }
    public static boolean areRepeatEventsEnabled() { return repeats; }
    public static int getKeyCount() { return KEYBOARD_SIZE; }
    public static char getEventCharacter() { return current != null ? current.ch : (char) 0; }
    public static int getEventKey() { return current != null ? current.key : KEY_NONE; }
    public static boolean getEventKeyState() { return current != null && current.down; }
    public static long getEventNanoseconds() { return current != null ? current.nanos : 0L; }
    public static boolean isRepeatEvent() { return current != null && current.repeat; }
}
