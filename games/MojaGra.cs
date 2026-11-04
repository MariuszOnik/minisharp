// Szablon gry. Skopiuj plik, zmień nazwę i namespace (każda gra musi mieć własny namespace).
// Ctrl+Shift+B: sprawdza kod i wysyła go na 3DS.
namespace MojaGra;

class Game {
    static float x = 200, y = 120;

    static void Main() {
        Console.WriteLine("Start!");
    }

    static void Update() {
        x += Input.StickX() * 3;
        y -= Input.StickY() * 3;
        if (Input.Held(Btn.Left)) x -= 3;
        if (Input.Held(Btn.Right)) x += 3;
        if (Input.Held(Btn.Up)) y -= 3;
        if (Input.Held(Btn.Down)) y += 3;
        x = Math.Clamp(x, 12f, 388f);
        y = Math.Clamp(y, 12f, 228f);
    }

    static void Draw() {
        Gfx.Clear(Color.DarkGray);
        Gfx.Circle(x, y, 12, Color.Yellow);
        Gfx.Text(10, 10, "x=" + (int)x + " y=" + (int)y, Color.White);

        Gfx.Screen(Screen.Bottom);
        Gfx.Clear(Color.Black);
        Gfx.Text(10, 10, "Dolny ekran", Color.Cyan);
    }
}
