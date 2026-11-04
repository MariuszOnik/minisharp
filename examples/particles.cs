// Fajerwerki: List<T>, foreach, switch, char i operatory bitowe.
// A wystrzeliwuje rakietę, X zmienia kolory, dotyk dolnego ekranu wybucha w miejscu dotyku.
namespace Particles;

class Spark {
    public float x, y, vx, vy;
    public int life, color;

    public Spark(float x0, float y0, int c) {
        x = x0; y = y0; color = c;
        float a = Random.Float() * Math.PI * 2;
        float speed = 0.5f + Random.Float() * 3;
        vx = Math.Cos(a) * speed;
        vy = Math.Sin(a) * speed;
        life = 40 + Random.Next(40);
    }

    public bool Step() {
        x += vx; y += vy;
        vy += 0.05f;
        vx *= 0.98f;
        life--;
        return life > 0;
    }
}

class Game {
    static List<Spark> sparks = new List<Spark>();
    static int palette = 0;
    static int frame = 0;
    static string mode = "auto";

    static int PickColor() {
        switch (palette) {
            case 0: return Gfx.Rgb(255, 120 + Random.Next(136), 40);
            case 1: return Gfx.Rgb(80, 160 + Random.Next(96), 255);
            default:
                // random bright colour built from bits
                int r = 128 | Random.Next(256), g = 128 | Random.Next(256), b = 128 | Random.Next(256);
                return (r << 16) | (g << 8) | b;
        }
    }

    static void Burst(float x, float y, int n) {
        for (int i = 0; i < n; i++) sparks.Add(new Spark(x, y, PickColor()));
    }

    static void Update() {
        frame++;
        if (Input.Pressed(Btn.A)) Burst(200, 120, 120);
        if (Input.Pressed(Btn.X)) palette = (palette + 1) % 3;
        if (Input.Pressed(Btn.Y)) mode = mode == "auto" ? "manual" : "auto";
        if (Input.Touching()) Burst(Input.TouchX() * 400 / 320, Input.TouchY(), 8);
        if (mode == "auto" && frame % 45 == 0) Burst(60 + Random.Next(280), 40 + Random.Next(100), 80);

        // remove dead sparks from the end so indices stay valid
        for (int i = sparks.Count - 1; i >= 0; i--)
            if (!sparks[i].Step()) sparks.RemoveAt(i);
    }

    static void Draw() {
        Gfx.Clear(0x05050C);
        foreach (Spark s in sparks) {
            float r = s.life > 30 ? 2 : 1;
            Gfx.Rect(s.x, s.y, r, r, s.color);
        }
        Gfx.Text(6, 4, "sparks " + sparks.Count + "  " + Time.Fps() + " FPS", Color.White);

        Gfx.Screen(Screen.Bottom);
        Gfx.Clear(Color.Black);
        string names = "OBR";
        char p = names[palette];
        Gfx.Text(10, 10, "A: wybuch  X: kolory (" + p + ")  Y: " + mode, Color.Cyan);
        Gfx.Text(10, 30, "Dotknij ekranu!", Color.Gray);
    }
}
