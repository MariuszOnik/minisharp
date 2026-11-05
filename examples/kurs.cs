// KURS: mala gra, w ktorej jest wszystko, co umie MiniSharp. Objasnienia: Help (X w edytorze), tematy "Kurs 1" ... "Kurs 10".
// Lewo/Prawo: ruch statku, A: strzal, START: menu.
namespace Kurs;

// ---- Kurs 7: interfejs = obietnica "mozna mnie trafic" ----
interface ITrafiany {
    bool Trafiony(float px, float py);      // czy pocisk w punkcie (px, py) mnie trafil?
}

// ---- Kurs 5: klasa = przepis na obiekty ----
class Pocisk {
    public float x, y;
    public bool zywy = true;
    public Pocisk(float x, float y) { this.x = x; this.y = y; }
    public void Ruch() {
        y -= 6;                              // leci w gore
        if (y < 0) zywy = false;             // poza ekranem: do usuniecia
    }
    public void Rysuj() { Gfx.Rect(x - 1, y - 4, 3, 8, Color.Yellow); }
}

// ---- Kurs 6: klasa bazowa dla wszystkich wrogow ----
abstract class Wrog : ITrafiany {
    public float x, y;
    public int hp;
    public int punkty;
    public Action<Wrog> onSmierc;            // Kurs 8: zdarzenie, ktos moze sie podpiac

    public Wrog(float x, int hp, int punkty) {
        this.x = x; y = -10;
        this.hp = hp; this.punkty = punkty;
    }

    public virtual void Ruch() { y += 1; }   // virtual: klasa pochodna moze zmienic
    public abstract void Rysuj();            // abstract: kazdy wrog musi narysowac sie sam
    public abstract string Nazwa();

    public bool Trafiony(float px, float py) {
        if (Mathf.Abs(px - x) > 10 || Mathf.Abs(py - y) > 10) return false;
        hp--;
        if (hp == 0) onSmierc?.Invoke(this); // ?. = wywolaj, jesli ktos sie podpial
        return true;
    }
}

class Nietoperz : Wrog {
    float czas;
    public Nietoperz(float x) : base(x, 1, 10) { }
    public override void Ruch() {            // lata zygzakiem
        base.Ruch();                         // najpierw zwykly ruch w dol
        czas += 0.1f;
        x += Mathf.Sin(czas) * 2;
    }
    public override void Rysuj() { Gfx.Circle(x, y, 7, Color.Magenta); }
    public override string Nazwa() => "nietoperz";
}

class Kamien : Wrog {
    public Kamien(float x) : base(x, 3, 30) { }          // wolny, ale wytrzymaly
    public override void Ruch() { y += 0.5f; }
    public override void Rysuj() { Gfx.Rect(x - 9, y - 9, 18, 18, hp == 3 ? Color.Gray : Color.DarkGray); }
    public override string Nazwa() => "kamien";
}

class Game {
    static float statekX = 200;
    static List<Pocisk> pociski = new List<Pocisk>();
    static List<Wrog> wrogowie = new List<Wrog>();
    static int wynik = 0, zycia = 3, fala = 0;
    static Dictionary<string, int> zabite = new Dictionary<string, int>();   // Kurs 10: ile jakich wrogow
    static string komunikat = "";

    static void Main() {
        Random.Seed(5);
        StartCoroutine(Fale());              // Kurs 9: korutyna wypuszcza kolejne fale
    }

    // ---- Kurs 9: korutyna = metoda, ktora moze czekac ----
    static IEnumerator Fale() {
        while (zycia > 0) {
            fala++;
            komunikat = "Fala " + fala;
            yield return new WaitForSeconds(1.5f);
            komunikat = "";
            for (int i = 0; i < 3 + fala * 2; i++) {          // Kurs 3: petla
                Dodaj(i % 3 == 2 ? new Kamien(Random.Range(20, 380)) : new Nietoperz(Random.Range(20, 380)));
                yield return new WaitForSeconds(0.6f);
            }
            while (wrogowie.Count > 0) yield return null;      // czekaj, az fala zniknie
        }
    }

    // ---- Kurs 4: metoda = nazwany kawalek kodu ----
    static void Dodaj(Wrog w) {
        w.onSmierc += Nagroda;               // Kurs 8: podpinamy metode do zdarzenia
        w.onSmierc += x => Wybuch(x.x, x.y); //          i lambde
        wrogowie.Add(w);
    }

    static void Nagroda(Wrog w) {
        wynik += w.punkty;
        string nazwa = w.Nazwa();
        if (zabite.ContainsKey(nazwa)) zabite[nazwa]++;
        else zabite[nazwa] = 1;
    }

    static List<float> wybuchy = new List<float>();   // x, y, czas, x, y, czas...
    static void Wybuch(float x, float y) { wybuchy.Add(x); wybuchy.Add(y); wybuchy.Add(10); }

    static void Update() {
        if (zycia <= 0) return;
        // ---- Kurs 2: warunki i przyciski ----
        if (Input.Held(Btn.Left)) statekX -= 4;
        if (Input.Held(Btn.Right)) statekX += 4;
        statekX = Mathf.Clamp(statekX, 10, 390);
        if (Input.Pressed(Btn.A)) pociski.Add(new Pocisk(statekX, 215));

        foreach (Pocisk p in pociski) {
            p.Ruch();
            // Kurs 7: kazdy wrog to ITrafiany; Find szuka pierwszego trafionego (Kurs 8: lambda)
            ITrafiany cel = wrogowie.Find(w => w.Trafiony(p.x, p.y));
            if (cel != null) p.zywy = false;
        }
        foreach (Wrog w in wrogowie) {
            w.Ruch();                        // kazdy wrog rusza sie po swojemu (override)
            if (w.y > 230) { zycia--; w.hp = 0; }
        }
        // sprzatanie: RemoveAll usuwa wszystko, co pasuje do warunku
        pociski.RemoveAll(p => !p.zywy);
        wrogowie.RemoveAll(w => w.hp <= 0);
        for (int i = wybuchy.Count - 1; i >= 2; i -= 3) {
            wybuchy[i]--;
            if (wybuchy[i] <= 0) { wybuchy.RemoveAt(i); wybuchy.RemoveAt(i - 1); wybuchy.RemoveAt(i - 2); }
        }
    }

    static void Draw() {
        Gfx.Screen(Screen.Top);
        Gfx.Clear(Gfx.Rgb(10, 10, 30));
        Gfx.Rect(statekX - 10, 220, 20, 10, Color.Cyan);       // Kurs 1: rysowanie
        foreach (Pocisk p in pociski) p.Rysuj();
        foreach (Wrog w in wrogowie) w.Rysuj();
        for (int i = 0; i < wybuchy.Count; i += 3) Gfx.Circle(wybuchy[i], wybuchy[i + 1], 12 - wybuchy[i + 2], Color.Orange);
        if (komunikat != "") Gfx.Text(160, 100, komunikat, 2, Color.White);
        if (zycia <= 0) Gfx.Text(130, 100, "KONIEC GRY", 2, Color.Red);

        Gfx.Screen(Screen.Bottom);
        Gfx.Clear(Color.Black);
        Gfx.Text(8, 8, "Wynik: " + wynik + "   Zycia: " + zycia + "   Fala: " + fala, Color.White);
        int y = 40;
        foreach (var kv in zabite) { Gfx.Text(8, y, kv.Key + ": " + kv.Value, Color.Gray); y += 16; }
        // najblizszy wrog (Kurs 8: Sort z lambda na kopii listy)
        var kolejka = new List<Wrog>(wrogowie);
        kolejka.Sort((a, b) => (int)(b.y - a.y));
        if (kolejka.Count > 0) Gfx.Text(8, 200, "Najblizej: " + kolejka[0].Nazwa(), Color.Yellow);
    }
}
