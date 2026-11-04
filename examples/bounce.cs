// Odbijajace sie kulki. D-pad steruje paletka, A dodaje kulke, START wraca do menu.
namespace Bounce;

class Ball {
    public float x, y, vx, vy, r;
    public int color;

    public Ball(float x0, float y0) {
        x = x0; y = y0;
        vx = Random.Range(-3, 4) + 0.5f;
        vy = Random.Range(1, 4);
        r = Random.Range(4, 10);
        color = Gfx.Rgb(Random.Range(80, 256), Random.Range(80, 256), Random.Range(80, 256));
    }

    public void Move(float padX) {
        x += vx; y += vy;
        if (x < r) { x = r; vx = -vx; }
        if (x > 400 - r) { x = 400 - r; vx = -vx; }
        if (y < r) { y = r; vy = -vy; }
        if (y > 220 - r && y < 230 && x > padX && x < padX + 60) { y = 220 - r; vy = -Math.Abs(vy); Game.score++; }
        if (y > 240 + r) { y = r; Game.lost++; }
    }
}

class Game {
    public static int score, lost;
    static Ball[] balls = new Ball[32];
    static int count = 0;
    static float padX = 170;

    static void Main() {
        Random.Seed(1234);
        for (int i = 0; i < 5; i++) Add();
    }

    static void Add() {
        if (count < balls.Length) { balls[count] = new Ball(200, 40); count++; }
    }

    static void Update() {
        if (Input.Held(Btn.Left)) padX -= 4;
        if (Input.Held(Btn.Right)) padX += 4;
        padX += Input.StickX() * 5;
        padX = Math.Clamp(padX, 0, 340);
        if (Input.Pressed(Btn.A)) Add();
        for (int i = 0; i < count; i++) balls[i].Move(padX);
    }

    static void Draw() {
        Gfx.Screen(Screen.Top);
        Gfx.Clear(Gfx.Rgb(20, 24, 40));
        for (int i = 0; i < count; i++) Gfx.Circle(balls[i].x, balls[i].y, balls[i].r, balls[i].color);
        Gfx.Rect(padX, 220, 60, 8, Color.White);

        Gfx.Screen(Screen.Bottom);
        Gfx.Clear(Color.DarkGray);
        Gfx.Text(10, 10, "Punkty: " + score, Color.Yellow);
        Gfx.Text(10, 30, "Stracone: " + lost, Color.Red);
        Gfx.Text(10, 50, "Kulki: " + count + "  (A = dodaj)", Color.White);
        for (int i = 0; i < 10; i++) Gfx.Rect(10 + i * 30, 190, 20, 20 + (score + i * 7) % 20, Color.Cyan);
    }
}
