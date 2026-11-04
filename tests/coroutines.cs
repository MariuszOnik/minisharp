// Coroutines like in Unity. On the PC host Time.Millis() advances 16 ms per frame.
using System.Collections;

class Enemy {
    public string name;
    public int hp = 3;
    public Enemy(string n) { name = n; }

    public IEnumerator Blink(int times) {          // instance coroutine with a parameter
        for (int i = 0; i < times; i++) {
            Console.WriteLine(name + " blink " + i + " @" + Game.frame);
            yield return null;
        }
    }
}

class Game {
    public static int frame = 0;
    static Coroutine ticker;
    static int ticks = 0;

    static IEnumerator Countdown() {
        Console.WriteLine("countdown start @" + frame);
        yield return new WaitForSeconds(0.1f);      // 100 ms = about 7 frames
        Console.WriteLine("after 0.1s @" + frame);
        yield return Child("A");                    // waits until Child ends
        Console.WriteLine("child done @" + frame);
        var e = new Enemy("bat");
        yield return StartCoroutine(e.Blink(2));
        Console.WriteLine("countdown end @" + frame);
    }

    static IEnumerator Child(string tag) {
        Console.WriteLine("child " + tag + " start @" + frame);
        yield return null;
        yield return null;
        Console.WriteLine("child " + tag + " end @" + frame);
    }

    static IEnumerator Ticker() {
        while (true) {
            ticks++;
            if (ticks == 3) Console.WriteLine("ticker 3 @" + frame);
            yield return 0;
        }
    }

    static IEnumerator Early() {
        Console.WriteLine("early 1");
        if (frame >= 0) yield break;
        Console.WriteLine("never");
    }

    static void Main() {
        StartCoroutine(Countdown());
        ticker = StartCoroutine(Ticker());
        StartCoroutine(Early());
        Console.WriteLine("main done");
    }

    static void Update() {
        frame++;
        if (frame == 10) { StopCoroutine(ticker); Console.WriteLine("ticker stopped, ticks " + ticks); }
        if (frame == 30) { App.Quit(); }
    }
}
