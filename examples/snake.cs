// Waz. D-pad steruje, zjadaj czerwone pola. START wraca do menu.
namespace SnakeGame;

class Snake {
    const int W = 25;
    const int H = 15;
    const int CELL = 16;

    static int[] xs = new int[W * H];
    static int[] ys = new int[W * H];
    static int len, dir, timer, foodX, foodY, best;
    static bool dead;

    static void Main() { Reset(); }

    static void Reset() {
        len = 3; dir = 1; timer = 0; dead = false;
        for (int i = 0; i < len; i++) { xs[i] = 5 - i; ys[i] = 7; }
        PlaceFood();
    }

    static void PlaceFood() {
        while (true) {
            foodX = Random.Next(W); foodY = Random.Next(H);
            if (!Occupied(foodX, foodY)) return;
        }
    }

    static bool Occupied(int x, int y) {
        for (int i = 0; i < len; i++) if (xs[i] == x && ys[i] == y) return true;
        return false;
    }

    static void Update() {
        if (dead) { if (Input.Pressed(Btn.A)) Reset(); return; }
        // 0 = gora, 1 = prawo, 2 = dol, 3 = lewo; nie mozna zawrocic w miejscu
        if (Input.Pressed(Btn.Up) && dir != 2) dir = 0;
        else if (Input.Pressed(Btn.Right) && dir != 3) dir = 1;
        else if (Input.Pressed(Btn.Down) && dir != 0) dir = 2;
        else if (Input.Pressed(Btn.Left) && dir != 1) dir = 3;

        timer++;
        if (timer < 8) return;
        timer = 0;

        int nx = xs[0], ny = ys[0];
        if (dir == 0) ny--; else if (dir == 1) nx++; else if (dir == 2) ny++; else nx--;
        if (nx < 0 || ny < 0 || nx >= W || ny >= H || Occupied(nx, ny)) {
            dead = true;
            best = Math.Max(best, len - 3);
            return;
        }
        bool eat = nx == foodX && ny == foodY;
        if (eat) len++;
        for (int i = len - 1; i > 0; i--) { xs[i] = xs[i - 1]; ys[i] = ys[i - 1]; }
        xs[0] = nx; ys[0] = ny;
        if (eat) PlaceFood();
    }

    static void Draw() {
        Gfx.Screen(Screen.Top);
        Gfx.Clear(Gfx.Rgb(16, 40, 24));
        Gfx.Rect(foodX * CELL + 2, foodY * CELL + 2, CELL - 4, CELL - 4, Color.Red);
        for (int i = 0; i < len; i++) {
            int c = i == 0 ? Color.Yellow : Color.Green;
            Gfx.Rect(xs[i] * CELL + 1, ys[i] * CELL + 1, CELL - 2, CELL - 2, c);
        }
        Gfx.Screen(Screen.Bottom);
        Gfx.Clear(Color.Black);
        Gfx.Text(10, 10, "Dlugosc: " + len, Color.White);
        Gfx.Text(10, 30, "Rekord: " + best, Color.Yellow);
        if (dead) Gfx.Text(10, 80, "Koniec! A = od nowa", 0.7f, Color.Red);
    }
}
