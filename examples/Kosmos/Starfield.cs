namespace Kosmos;

class Starfield {
    float[] xs, ys, speed;

    public Starfield(int count) {
        xs = new float[count]; ys = new float[count]; speed = new float[count];
        for (int i = 0; i < count; i++) {
            xs[i] = Random.Range(0, 400);
            ys[i] = Random.Range(0, 240);
            speed[i] = Random.Range(1, 4);
        }
    }

    public void Move() {
        for (int i = 0; i < xs.Length; i++) {
            ys[i] += speed[i];
            if (ys[i] > 240) { ys[i] = 0; xs[i] = Random.Range(0, 400); }
        }
    }

    public void Draw() {
        for (int i = 0; i < xs.Length; i++) {
            int c = speed[i] > 2 ? Color.White : Color.Gray;
            Gfx.Rect(xs[i], ys[i], speed[i], speed[i], c);
        }
    }
}
