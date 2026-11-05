// LOCH: roguelike z liter. Turowa walka, komponenty jak w Unity, maszyna stanow (FSM) potworow.
// Objasnienia: Help, tematy "Loch 1" ... "Loch 4".
// D-pad: ruch / atak (wejdz na potwora), A: czekaj ture, X: wypij miksture, START: menu.
// @ ty, r szczur, g goblin, O ork, ! mikstura, > schody w dol.
namespace Loch;

// ================= Komponenty (Loch 2) =================
// Byt to "GameObject": ma pozycje, wyglad i liste komponentow. Zachowanie daja komponenty.
abstract class Komponent {
    public Byt byt;                          // wlasciciel (jak gameObject w Unity)
    public virtual void Tura() { }           // wolane w turze potworow
}

class Byt {
    public string nazwa;
    public char znak;
    public int kolor;
    public int x, y;
    List<Komponent> komponenty = new List<Komponent>();

    public Byt(string nazwa, char znak, int kolor, int x, int y) {
        this.nazwa = nazwa; this.znak = znak; this.kolor = kolor; this.x = x; this.y = y;
    }
    public Byt Dodaj(Komponent k) { k.byt = this; komponenty.Add(k); return this; }   // zwraca siebie: Dodaj(..).Dodaj(..)
    public Komponent Pobierz(Predicate<Komponent> jaki) => komponenty.Find(jaki);    // jak GetComponent
    public Zdrowie PobierzZdrowie() => Pobierz(k => k is Zdrowie) as Zdrowie;
    public Atak PobierzAtak() => Pobierz(k => k is Atak) as Atak;
    public Przedmiot PobierzPrzedmiot() => Pobierz(k => k is Przedmiot) as Przedmiot;
    public bool Zywy() { Zdrowie z = PobierzZdrowie(); return z != null && z.hp > 0; }
    public void Tura() { foreach (Komponent k in komponenty) k.Tura(); }
}

class Zdrowie : Komponent {
    public int hp, max;
    public Action<Byt> onSmierc;             // zdarzenie: kto chce, ten slucha
    public Zdrowie(int max) { this.max = max; hp = max; }
    public void Rana(int ile) {
        if (hp <= 0) return;
        hp -= ile;
        if (hp <= 0) { hp = 0; onSmierc?.Invoke(byt); }
    }
    public void Lecz(int ile) { hp = Math.Min(max, hp + ile); }
}

class Atak : Komponent {
    public int sila;
    public Atak(int sila) { this.sila = sila; }
    public void Uderz(Byt cel) {
        int obrazenia = sila + Random.Range(0, 3);
        Game.Log(byt.nazwa + " -> " + cel.nazwa + ": -" + obrazenia);
        cel.PobierzZdrowie().Rana(obrazenia);
    }
}

class Przedmiot : Komponent {
    public Action<Byt> efekt;                // co robi z tym, kto go uzyje
    public Przedmiot(Action<Byt> efekt) { this.efekt = efekt; }
}

// ================= Maszyna stanow (Loch 3) =================
// Kazdy stan to klasa. Tura() zwraca nowy stan albo null, gdy potwor zostaje w obecnym.
abstract class Stan {
    public abstract string Nazwa();
    public virtual void Wejdz(AI ai) { }
    public abstract Stan Tura(AI ai);
}

class Spi : Stan {
    public override string Nazwa() => "spi";
    public override Stan Tura(AI ai) {
        if (ai.OdlegloscDoGracza() <= 4) return new Goni();
        return null;
    }
}

class Goni : Stan {
    public override string Nazwa() => "goni";
    public override void Wejdz(AI ai) { Game.Log(ai.byt.nazwa + " cie zauwaza!"); }
    public override Stan Tura(AI ai) {
        Zdrowie z = ai.byt.PobierzZdrowie();
        if (z.hp * 3 < z.max && ai.tchorz) return new Ucieka();
        if (ai.OdlegloscDoGracza() == 1) ai.byt.PobierzAtak().Uderz(Game.gracz);
        else ai.Krok(1);
        if (ai.OdlegloscDoGracza() > 9) return new Spi();     // zgubil cie
        return null;
    }
}

class Ucieka : Stan {
    public override string Nazwa() => "ucieka";
    public override void Wejdz(AI ai) { Game.Log(ai.byt.nazwa + " ucieka!"); }
    public override Stan Tura(AI ai) {
        ai.Krok(-1);                         // krok OD gracza
        Zdrowie z = ai.byt.PobierzZdrowie();
        z.Lecz(1);                           // liże rany
        if (z.hp * 2 >= z.max) return new Goni();
        return null;
    }
}

class AI : Komponent {
    public Stan stan = new Spi();
    public bool tchorz;
    public AI(bool tchorz) { this.tchorz = tchorz; }
    public override void Tura() {
        if (!byt.Zywy()) return;
        Stan nowy = stan.Tura(this);
        if (nowy != null) { stan = nowy; stan.Wejdz(this); }
    }
    public int OdlegloscDoGracza() => Math.Max(Math.Abs(Game.gracz.x - byt.x), Math.Abs(Game.gracz.y - byt.y));
    // kierunek 1: do gracza, -1: od gracza
    public void Krok(int kierunek) {
        int dx = Math.Sign(Game.gracz.x - byt.x) * kierunek, dy = Math.Sign(Game.gracz.y - byt.y) * kierunek;
        if (dx != 0 && Game.Wolne(byt.x + dx, byt.y)) byt.x += dx;
        else if (dy != 0 && Game.Wolne(byt.x, byt.y + dy)) byt.y += dy;
    }
}

// ================= Gra (Loch 1 i 4) =================
class Game {
    const int W = 40, H = 19, KX = 10, KY = 12;          // mapa 40 x 19 pol, pole 10 x 12 pikseli
    static char[] mapa = new char[W * H];                // '#' sciana, '.' podloga, '>' schody
    static bool[] widziane = new bool[W * H];
    static string[] znaki = new string[128];             // gotowe napisy "a", "b"... (bez smieci co klatke)
    public static Byt gracz;
    static List<Byt> byty = new List<Byt>();
    static List<string> log = new List<string>();
    static int poziom = 1, mikstury = 0;

    public static void Log(string tekst) {
        log.Add(tekst);
        Console.WriteLine(tekst);                       // trafia tez do log.txt na karcie
        if (log.Count > 9) log.RemoveAt(0);
    }

    static void Main() {
        for (int i = 32; i < 128; i++) znaki[i] = "" + (char)i;
        Random.Seed(Time.Millis());
        NowaGra();
    }

    static void NowaGra() {
        poziom = 1; mikstury = 1;
        gracz = new Byt("Bohater", '@', Color.White, 0, 0).Dodaj(new Zdrowie(30)).Dodaj(new Atak(3));
        gracz.PobierzZdrowie().onSmierc += b => Log("ZGINALES. A: nowa gra");
        log.Clear();
        Log("Witaj w lochu! Znajdz schody >");
        Generuj();
    }

    // ---- Loch 1: generowanie: pokoje polaczone korytarzami ----
    static void Generuj() {
        for (int i = 0; i < W * H; i++) { mapa[i] = '#'; widziane[i] = false; }
        byty.Clear();
        byty.Add(gracz);
        var pokoje = new List<int[]>();                  // kazdy pokoj: { x, y, szer, wys }
        for (int proba = 0; proba < 80 && pokoje.Count < 8; proba++) {
            int w = Random.Range(4, 11), h = Random.Range(3, 6);
            int x = Random.Range(1, W - w - 1), y = Random.Range(1, H - h - 1);
            if (pokoje.Exists(p => x <= p[0] + p[2] && p[0] <= x + w && y <= p[1] + p[3] && p[1] <= y + h)) continue;   // nachodzi
            for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + w; xx++) mapa[yy * W + xx] = '.';
            if (pokoje.Count > 0) {
                int[] p = pokoje[pokoje.Count - 1];
                Korytarz(p[0] + p[2] / 2, p[1] + p[3] / 2, x + w / 2, y + h / 2);
            }
            pokoje.Add(new int[] { x, y, w, h });
        }
        int[] start = pokoje[0], koniec = pokoje[pokoje.Count - 1];
        gracz.x = start[0] + start[2] / 2; gracz.y = start[1] + start[3] / 2;
        mapa[(koniec[1] + koniec[3] / 2) * W + koniec[0] + koniec[2] / 2] = '>';
        for (int i = 1; i < pokoje.Count; i++) {
            int[] p = pokoje[i];
            int ile = Random.Range(1, 3) + poziom / 3;
            for (int k = 0; k < ile; k++) Postaw(NowyPotwor(), p);
            if (Random.Range(0, 3) == 0)
                Postaw(new Byt("mikstura", '!', Color.Magenta, 0, 0).Dodaj(new Przedmiot(kto => kto.PobierzZdrowie().Lecz(12))), p);
        }
        Odkryj();
    }

    static void Korytarz(int x1, int y1, int x2, int y2) {
        for (int x = Math.Min(x1, x2); x <= Math.Max(x1, x2); x++) mapa[y1 * W + x] = '.';
        for (int y = Math.Min(y1, y2); y <= Math.Max(y1, y2); y++) mapa[y * W + x2] = '.';
    }

    static void Postaw(Byt b, int[] pokoj) {
        for (int proba = 0; proba < 20; proba++) {
            int x = pokoj[0] + Random.Range(0, pokoj[2]), y = pokoj[1] + Random.Range(0, pokoj[3]);
            if (mapa[y * W + x] == '.' && BytNa(x, y) == null) { b.x = x; b.y = y; byty.Add(b); return; }
        }
    }

    // ---- Loch 2: potwory skladane z komponentow ----
    static Byt NowyPotwor() {
        int r = Random.Range(0, 10) + poziom;
        Byt b;
        if (r < 6) b = new Byt("Szczur", 'r', Color.Gray, 0, 0).Dodaj(new Zdrowie(4)).Dodaj(new Atak(1)).Dodaj(new AI(true));
        else if (r < 10) b = new Byt("Goblin", 'g', Color.Green, 0, 0).Dodaj(new Zdrowie(8)).Dodaj(new Atak(2)).Dodaj(new AI(true));
        else b = new Byt("Ork", 'O', Color.Red, 0, 0).Dodaj(new Zdrowie(15)).Dodaj(new Atak(4)).Dodaj(new AI(false));
        b.PobierzZdrowie().onSmierc += m => Log(m.nazwa + " ginie.");
        return b;
    }

    static Byt BytNa(int x, int y) => byty.Find(b => b.x == x && b.y == y && b != gracz);

    public static bool Wolne(int x, int y) {
        if (mapa[y * W + x] == '#') return false;
        if (gracz.x == x && gracz.y == y) return false;
        Byt b = BytNa(x, y);
        return b == null || !b.Zywy();
    }

    static bool Widac(int x, int y) => (x - gracz.x) * (x - gracz.x) + (y - gracz.y) * (y - gracz.y) <= 30;

    static void Odkryj() {
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (Widac(x, y)) widziane[y * W + x] = true;
    }

    // ---- Loch 4: tura gracza, potem tura potworow ----
    static void Update() {
        if (!gracz.Zywy()) { if (Input.Pressed(Btn.A)) NowaGra(); return; }
        int dx = 0, dy = 0;
        if (Input.Pressed(Btn.Left)) dx = -1;
        else if (Input.Pressed(Btn.Right)) dx = 1;
        else if (Input.Pressed(Btn.Up)) dy = -1;
        else if (Input.Pressed(Btn.Down)) dy = 1;
        bool tura = false;
        if (dx != 0 || dy != 0) tura = RuchGracza(gracz.x + dx, gracz.y + dy);
        else if (Input.Pressed(Btn.A)) tura = true;                      // czekaj
        else if (Input.Pressed(Btn.X)) {
            if (mikstury > 0) { mikstury--; gracz.PobierzZdrowie().Lecz(12); Log("Pijesz miksture. +12 hp"); tura = true; }
            else Log("Nie masz mikstur.");
        }
        if (!tura) return;
        foreach (Byt b in byty) if (b != gracz) b.Tura();               // tura potworow: kazdy komponent robi swoje
        byty.RemoveAll(b => b.PobierzZdrowie() != null && !b.Zywy() && b != gracz);
        Odkryj();
    }

    // true = tura minela
    static bool RuchGracza(int x, int y) {
        if (mapa[y * W + x] == '#') return false;
        Byt cel = BytNa(x, y);
        if (cel != null && cel.Zywy()) { gracz.PobierzAtak().Uderz(cel); return true; }
        gracz.x = x; gracz.y = y;
        if (cel != null && cel.PobierzPrzedmiot() != null) {           // podnies
            mikstury++;
            byty.Remove(cel);
            Log("Podnosisz miksture (X: wypij).");
        }
        if (mapa[y * W + x] == '>') {
            poziom++;
            Log("Schodzisz nizej. Poziom " + poziom);
            Generuj();
        }
        return true;
    }

    static void Draw() {
        Gfx.Screen(Screen.Top);
        Gfx.Clear(Color.Black);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int i = y * W + x;
                if (!widziane[i]) continue;
                char c = mapa[i];
                bool teraz = Widac(x, y);
                if (c == '.' && !teraz) continue;                        // poza zasiegiem: tylko sciany
                int kolor = c == '#' ? (teraz ? Color.Gray : Color.DarkGray) : c == '>' ? Color.Yellow : Color.DarkGray;
                Gfx.Text(x * KX, y * KY, znaki[c], 0.45f, kolor);
            }
        foreach (Byt b in byty)
            if (Widac(b.x, b.y)) Gfx.Text(b.x * KX, b.y * KY, znaki[b.znak], 0.45f, b.kolor);
        Gfx.Text(gracz.x * KX, gracz.y * KY, znaki['@'], 0.45f, gracz.Zywy() ? Color.White : Color.Red);

        Gfx.Screen(Screen.Bottom);
        Gfx.Clear(Gfx.Rgb(15, 15, 25));
        Zdrowie z = gracz.PobierzZdrowie();
        Gfx.Text(6, 4, "HP " + z.hp + "/" + z.max + "   Mikstury " + mikstury + "   Poziom " + poziom, z.hp * 3 < z.max ? Color.Red : Color.White);
        int wy = 26;
        foreach (Byt b in byty)                                          // widoczne potwory i ich stan (FSM)
            if (b != gracz && b.Zywy() && Widac(b.x, b.y)) {
                AI ai = b.Pobierz(k => k is AI) as AI;
                Gfx.Text(6, wy, b.znak + " " + b.nazwa + " " + b.PobierzZdrowie().hp + " hp, " + ai.stan.Nazwa(), b.kolor);
                wy += 16;
            }
        for (int i = 0; i < log.Count; i++) Gfx.Text(6, 92 + i * 16, log[i], i == log.Count - 1 ? Color.Yellow : Color.Gray);
    }
}
