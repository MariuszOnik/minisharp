// Dziedziczenie i interfejsy. Kazdy ksztalt sam wie, jak sie rusza i rysuje (override).
// A dodaje ksztalt, dotyk na dolnym ekranie trafia w ksztalty (interfejs ITrafiany), START wraca do menu.
namespace Shapes;

interface ITrafiany {
    bool Trafiony(float tx, float ty);
}

abstract class Ksztalt : ITrafiany {
    public float x, y, vx, vy;
    public int kolor;

    public Ksztalt(float x, float y, int kolor) {
        this.x = x; this.y = y; this.kolor = kolor;
        vx = Random.Range(-20, 21) / 10.0f;
        vy = Random.Range(-20, 21) / 10.0f;
    }

    public abstract float Rozmiar();
    public abstract void Rysuj();

    public virtual void Ruch() {
        x += vx; y += vy;
        float r = Rozmiar();
        if (x < r || x > 400 - r) vx = -vx;
        if (y < r || y > 240 - r) vy = -vy;
    }

    public virtual bool Trafiony(float tx, float ty) {
        float dx = tx - x, dy = ty - y;
        return dx * dx + dy * dy < Rozmiar() * Rozmiar();
    }

    public override string ToString() { return "ksztalt"; }
}

class Kolo : Ksztalt {
    public float r;
    public Kolo(float x, float y) : base(x, y, Color.Cyan) { r = Random.Range(8, 20); }
    public override float Rozmiar() { return r; }
    public override void Rysuj() { Gfx.Circle(x, y, r, kolor); }
    public override string ToString() { return "kolo r=" + (int)r; }
}

class Kwadrat : Ksztalt {
    public float a;
    public Kwadrat(float x, float y) : base(x, y, Color.Orange) { a = Random.Range(10, 30); }
    public override float Rozmiar() { return a / 2; }
    public override void Rysuj() { Gfx.Rect(x - a / 2, y - a / 2, a, a, kolor); }
    public override string ToString() { return "kwadrat a=" + (int)a; }
}

// Pulsujace kolo: dziedziczy po Kolo i zmienia tylko to, co trzeba
class Puls : Kolo {
    float t;
    public Puls(float x, float y) : base(x, y) { kolor = Color.Magenta; }
    public override void Ruch() {
        base.Ruch();
        t += 0.1f;
        r = 12 + Mathf.Sin(t) * 6;
    }
    public override string ToString() { return "puls (" + base.ToString() + ")"; }
}

class Game {
    static List<Ksztalt> ksztalty = new List<Ksztalt>();
    static string ostatni = "dotknij ksztaltu";
    static int trafien = 0;

    static void Main() {
        Random.Seed(7);
        for (int i = 0; i < 6; i++) Dodaj();
    }

    static void Dodaj() {
        float x = Random.Range(40, 360), y = Random.Range(40, 200);
        int rodzaj = ksztalty.Count % 3;
        Ksztalt k;
        if (rodzaj == 0) k = new Kolo(x, y);
        else if (rodzaj == 1) k = new Kwadrat(x, y);
        else k = new Puls(x, y);
        ksztalty.Add(k);
    }

    static void Update() {
        if (Input.Pressed(Btn.A) && ksztalty.Count < 40) Dodaj();
        foreach (Ksztalt k in ksztalty) k.Ruch();
        if (Input.Touching()) {
            // dolny ekran ma 320 px szerokosci: przeliczamy na gorny (400)
            float tx = Input.TouchX() * 400 / 320.0f, ty = Input.TouchY();
            for (int i = ksztalty.Count - 1; i >= 0; i--) {
                ITrafiany t = ksztalty[i];
                if (t.Trafiony(tx, ty)) {
                    ostatni = "trafiony: " + ksztalty[i];
                    if (ksztalty[i] is Puls p) ostatni += " (puls!)";
                    ksztalty.RemoveAt(i);
                    trafien++;
                    break;
                }
            }
        }
    }

    static void Draw() {
        Gfx.Screen(Screen.Top);
        Gfx.Clear(Gfx.Rgb(20, 20, 40));
        foreach (Ksztalt k in ksztalty) k.Rysuj();
        Gfx.Screen(Screen.Bottom);
        Gfx.Clear(Color.Black);
        Gfx.Text(8, 8, "Dziedziczenie: Kolo, Kwadrat, Puls", Color.White);
        Gfx.Text(8, 28, "A: dodaj   dotyk: traf", Color.Gray);
        Gfx.Text(8, 60, "ksztaltow: " + ksztalty.Count + "  trafien: " + trafien, Color.Yellow);
        Gfx.Text(8, 80, ostatni, Color.Cyan);
    }
}
