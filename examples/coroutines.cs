// Korutyny jak w Unity. Na dolnym ekranie widać każdą korutynę i jej stan.
//   Fale    - wypuszcza wrogów (B: zatrzymaj / wznów). Już lecące kulki porusza Update(), więc dolatują do końca.
//   Pisanie - tekst literka po literce (A: szybciej).
//   Miganie - kursor miga co 0.4 s, działa cały czas.
//   X       - StopAllCoroutines(): zatrzymuje wszystko naraz.
namespace CoroutineDemo;

class Enemy {
    public float x, y, speed;
    public Enemy(float y0) { x = -12; y = y0; speed = 1 + Random.Float() * 2; }
}

class Game {
    static List<Enemy> enemies = new List<Enemy>();
    static Coroutine waves, typing, blinking;
    static string shown = "";
    static bool cursor = false, fast = false;
    static int wave = 0, sentInWave = 0, waveSize = 0;
    static string wavesState = "", typingState = "", blinkState = "";

    static void Main() {
        typing = StartCoroutine(Typewriter("Korutyny dzialaja jak w Unity!"));
        blinking = StartCoroutine(Blink());
        waves = StartCoroutine(Waves());
    }

    static IEnumerator Typewriter(string text) {
        for (int i = 0; i < text.Length; i++) {
            shown += text[i];
            typingState = "pisze " + (i + 1) + "/" + text.Length;
            yield return new WaitForSeconds(fast ? 0.01f : 0.08f);
        }
        typingState = "skonczone (yield break)";
    }

    static IEnumerator Blink() {
        while (true) {
            cursor = !cursor;
            blinkState = cursor ? "wlaczony" : "wylaczony";
            yield return new WaitForSeconds(0.4f);
        }
    }

    static IEnumerator Waves() {
        while (true) {
            wave++;
            yield return SpawnWave(3 + wave);       // czeka, aż cała fala wyjdzie
            wavesState = "przerwa 2 s";
            yield return new WaitForSeconds(2);
        }
    }

    static IEnumerator SpawnWave(int count) {
        waveSize = count;
        for (sentInWave = 0; sentInWave < count; sentInWave++) {
            enemies.Add(new Enemy(30 + Random.Next(190)));
            wavesState = "fala " + wave + ": wysyla " + (sentInWave + 1) + "/" + count;
            yield return new WaitForSeconds(0.3f);
        }
    }

    static void Update() {
        if (Input.Pressed(Btn.A)) fast = true;
        if (Input.Pressed(Btn.B)) {
            if (waves != null) { StopCoroutine(waves); waves = null; wavesState = "ZATRZYMANE (B wznawia)"; }
            else { waves = StartCoroutine(Waves()); }
        }
        if (Input.Pressed(Btn.X)) {
            StopAllCoroutines();
            waves = null;
            wavesState = "ZATRZYMANE"; typingState = "ZATRZYMANE"; blinkState = "ZATRZYMANE";
        }
        // kulki porusza Update, nie korutyna
        for (int i = enemies.Count - 1; i >= 0; i--) {
            enemies[i].x += enemies[i].speed;
            if (enemies[i].x > 410) enemies.RemoveAt(i);
        }
    }

    static void Draw() {
        Gfx.Clear(0x101820);
        foreach (var e in enemies) Gfx.Circle(e.x, e.y, 8, Color.Red);
        Gfx.Text(6, 4, "kulek na ekranie: " + enemies.Count, Color.Yellow);

        Gfx.Screen(Screen.Bottom);
        Gfx.Clear(Color.Black);
        Gfx.Text(8, 6, shown + (cursor ? "_" : ""), Color.White);
        Gfx.Text(8, 40, "Korutyny:", Color.Cyan);
        Gfx.Text(8, 60, "Fale:    " + wavesState, waves != null ? Color.Green : Color.Red);
        Gfx.Text(8, 78, "Pisanie: " + typingState, Color.Green);
        Gfx.Text(8, 96, "Miganie: " + blinkState, Color.Green);
        Gfx.Text(8, 130, "Lecace kulki porusza Update(), nie korutyna,", Color.Gray);
        Gfx.Text(8, 146, "wiec po stopie dolatuja do konca ekranu.", Color.Gray);
        Gfx.Text(8, 200, "A: szybciej  B: fale stop/start", Color.Gray);
        Gfx.Text(8, 216, "X: StopAllCoroutines()", Color.Gray);
    }
}
