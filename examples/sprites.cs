// Sprite'y i dźwięk: bohater z animacją chodu (sprite sheet 4 x 16x16), monety, dźwięki.
// D-pad / circle pad: chodzenie, A: skok. Pliki hero.png, coin.png, grass.png, coin.wav, jump.wav
// leżą obok programu (w sdmc:/minisharp/).
namespace SpritesDemo;

class Coin {
    public float x, y, phase;
    public Coin(float x0, float y0) { x = x0; y = y0; phase = Random.Float() * 6; }
}

class Game {
    const int SCALE = 3;                    // 16x16 pixel art drawn 3x bigger
    static int hero, coin, grass, coinSnd, jumpSnd;
    static float x = 100, y = 0, vy = 0;
    static bool facingLeft = false, walking = false;
    static int frame = 0, score = 0;
    static List<Coin> coins = new List<Coin>();
    static float ground = 240 - 16 * SCALE - 16 * SCALE;

    static void Main() {
        hero = Gfx.LoadImage("hero.png");
        coin = Gfx.LoadImage("coin.png");
        grass = Gfx.LoadImage("grass.png");
        coinSnd = Audio.Load("coin.wav");
        jumpSnd = Audio.Load("jump.wav");
        y = ground;
        for (int i = 0; i < 5; i++) coins.Add(new Coin(40 + i * 75, ground - 30 - Random.Next(60)));
        StartCoroutine(Animate());
    }

    static IEnumerator Animate() {          // walking animation: next frame every 0.12 s
        while (true) {
            frame = walking ? (frame + 1) % 4 : 0;
            yield return new WaitForSeconds(0.12f);
        }
    }

    static void Update() {
        float dx = Input.StickX() * 2.5f;
        if (Input.Held(Btn.Left)) dx = -2.5f;
        if (Input.Held(Btn.Right)) dx = 2.5f;
        walking = dx != 0;
        if (dx < 0) facingLeft = true;
        if (dx > 0) facingLeft = false;
        x = Mathf.Clamp(x + dx, 0, 400 - 16 * SCALE);

        if (Input.Pressed(Btn.A) && y >= ground) { vy = -7; Audio.Play(jumpSnd, 0.6f); }
        vy += 0.35f;
        y = Mathf.Min(y + vy, ground);

        for (int i = coins.Count - 1; i >= 0; i--) {
            Coin c = coins[i];
            float cx = c.x + 6 * 2, cy = c.y + 6 * 2;
            float hx = x + 8 * SCALE, hy = y + 8 * SCALE;
            if (Mathf.Abs(cx - hx) < 24 && Mathf.Abs(cy - hy) < 28) {
                coins.RemoveAt(i);
                score++;
                Audio.Play(coinSnd);
                coins.Add(new Coin(20 + Random.Next(340), ground - 30 - Random.Next(70)));
            }
        }
    }

    static void Draw() {
        Gfx.Clear(0x6EC6FF);
        // ground: tiles drawn 3x bigger
        for (int tx = 0; tx < 400; tx += 16 * SCALE) {
            Gfx.Image(grass, tx, ground + 16 * SCALE, SCALE);
        }
        // coins: they pulse (scale) and turn (angle)
        float t = Time.Millis() / 1000f;
        foreach (Coin c in coins) {
            float s = 2 + Mathf.Sin(t * 4 + c.phase) * 0.25f;
            Gfx.ImageEx(coin, c.x, c.y, s, s, Mathf.Sin(t * 2 + c.phase) * 20);
        }
        // hero: one 16x16 frame from the sheet; a negative scale mirrors him when walking left
        Gfx.ImagePartEx(hero, x, y, frame * 16, 0, 16, 16, facingLeft ? -SCALE : SCALE, SCALE, 0);
        Gfx.Text(8, 6, "Monety: " + score, 0.7f, Color.White);

        Gfx.Screen(Screen.Bottom);
        Gfx.Clear(0x202830);
        Gfx.Text(10, 10, "Sprite'y i dzwiek", Color.Yellow);
        Gfx.Text(10, 40, "D-pad: chodzenie (animacja z arkusza)", Color.White);
        Gfx.Text(10, 58, "A: skok (dzwiek jump.wav)", Color.White);
        Gfx.Text(10, 76, "Monety: coin.wav, obrot i skala", Color.White);
        // the whole sprite sheet, 4x bigger, so you can see the frames
        Gfx.Image(hero, 32, 120, 4);
        Gfx.Rect(32 + frame * 64, 186, 64, 3, Color.Yellow);
        Gfx.Text(10, 200, "klatka " + frame + " z arkusza hero.png", Color.Gray);
    }
}
