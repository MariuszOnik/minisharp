// The small programs from the Help topics "Kurs 1" ... "Kurs 4" (the rest comes from examples/kurs.cs).
class Game {
    // Kurs 2
    static float x = 200, y = 120;
    static int kolor = Color.Green;
    // Kurs 3
    static List<float> gwiazdyX = new List<float>();
    static List<float> gwiazdyY = new List<float>();

    static void Main() {
        // Kurs 1
        string imie = "Rycerz";
        int hp = 100;
        int obrazenia = 15;
        float szybkosc = 2.5f;
        bool zyje = true;
        hp = hp - obrazenia;
        hp -= obrazenia;
        Console.WriteLine(imie + " ma " + hp + " hp");
        Console.WriteLine("Za 3 sekundy przejdzie " + szybkosc * 3 * 60 + " pikseli");
        if (hp <= 0) zyje = false;
        Console.WriteLine("Zyje: " + zyje);
        // Kurs 3
        for (int i = 0; i < 50; i++) {
            gwiazdyX.Add(Random.Range(0, 400));
            gwiazdyY.Add(Random.Range(0, 240));
        }
        // Kurs 4
        Console.WriteLine(Odleglosc(0, 0, 3, 4));
        Console.WriteLine(Trafiony(10, 10, 12, 11, 5));
    }

    static void Update() {
        // Kurs 2
        if (Input.Held(Btn.Left)) x -= 3;
        if (Input.Held(Btn.Right)) x += 3;
        if (Input.Held(Btn.Up)) y -= 3;
        if (Input.Held(Btn.Down)) y += 3;
        if (x < 0) x = 0;
        if (x > 400) x = 400;
        if (Input.Pressed(Btn.A)) kolor = Color.Red;
        // Kurs 3
        for (int i = 0; i < gwiazdyY.Count; i++) {
            gwiazdyY[i] += 1;
            if (gwiazdyY[i] > 240) gwiazdyY[i] = 0;
        }
    }

    static void Draw() {
        Gfx.Screen(Screen.Top);
        Gfx.Clear(Color.Black);
        RysujGwiazdy();
        Gfx.Circle(x, y, 10, kolor);
        Gfx.Text(5, 5, "x = " + (int)x, Color.White);
    }

    // Kurs 4
    static void RysujGwiazdy() {
        for (int i = 0; i < gwiazdyX.Count; i++) Gfx.Rect(gwiazdyX[i], gwiazdyY[i], 2, 2, Color.White);
    }
    static float Odleglosc(float x1, float y1, float x2, float y2) {
        float dx = x2 - x1, dy = y2 - y1;
        return Mathf.Sqrt(dx * dx + dy * dy);
    }
    static bool Trafiony(float ax, float ay, float bx, float by, float promien) {
        return Odleglosc(ax, ay, bx, by) < promien;
    }
}
