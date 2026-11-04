using System;

class Vec {
    public float x, y;
    public Vec(float x0, float y0) { x = x0; y = y0; }
    public float Len() { return Math.Sqrt(x * x + y * y); }
    public Vec Add(Vec o) { return new Vec(x + o.x, y + o.y); }
}

class Counter {
    static int total = 100;
    int count = 5;
    public void Inc() { count++; total++; }
    public int Get() { return count; }
    public static int Total() { return total; }
}

class Node {
    public int value;
    public Node next;
}

class Program {
    static int Fib(int n) { return n < 2 ? n : Fib(n - 1) + Fib(n - 2); }

    static string Grade(int score) {
        if (score >= 90) return "A";
        else if (score >= 75) return "B";
        return "C";
    }

    static void Main() {
        Console.WriteLine("Hello from MiniSharp!");
        int a = 7, b;
        b = 3;
        Console.WriteLine(a + b);
        Console.WriteLine(a / b);
        Console.WriteLine(a % b);
        Console.WriteLine(a / 2.0f);
        Console.WriteLine("a=" + a + " b=" + b);
        Console.WriteLine(1 + 2 + "x" + 1 + 2);
        float f = 1.5f;
        f += 2;
        Console.WriteLine(f);
        Console.WriteLine((int)7.9f);
        Console.WriteLine(-a);
        Console.WriteLine(true && !false);

        var v = new Vec(3, 4);
        Console.WriteLine(v.Len());
        Vec w = v.Add(new Vec(1, 1));
        Console.WriteLine(w.x + "," + w.y);

        Counter c = new Counter();
        c.Inc(); c.Inc();
        Console.WriteLine(c.Get());
        Console.WriteLine(Counter.Total());

        int[] arr = new int[5];
        for (int i = 0; i < arr.Length; i++) arr[i] = i * i;
        int sum = 0;
        for (int i = 0; i < arr.Length; i++) sum += arr[i];
        Console.WriteLine(sum);

        string[] names = new string[] { "Ala", "Ola", "Ela" };
        string all = "";
        for (int i = 0; i < names.Length; i++) {
            if (i > 0) all += ", ";
            all += names[i];
        }
        Console.WriteLine(all);

        int[][] grid = new int[3][];
        for (int i = 0; i < 3; i++) { grid[i] = new int[3]; grid[i][i] = 1; }
        Console.WriteLine(grid[1][1] + grid[1][0]);

        Console.WriteLine(Fib(20));
        Console.WriteLine(Grade(95) + Grade(80) + Grade(10));

        int k = 0;
        while (true) { k++; if (k == 3) continue; if (k > 5) break; }
        Console.WriteLine(k);
        int d = 0;
        do { d += 2; } while (d < 7);
        Console.WriteLine(d);

        int x = 5;
        int y = x++;
        int z = ++x;
        Console.WriteLine(x + " " + y + " " + z);
        int[] q = new int[] { 10, 20 };
        int idx = 0;
        q[idx++] += 5;
        Console.WriteLine(q[0] + " " + q[1] + " " + idx);

        Node head = null;
        for (int i = 1; i <= 4; i++) { Node n = new Node(); n.value = i; n.next = head; head = n; }
        int s = 0;
        for (Node n = head; n != null; n = n.next) s += n.value;
        Console.WriteLine(s);

        string str = "abc";
        Console.WriteLine(str.Length);
        Console.WriteLine(str == "abc");
        Console.WriteLine(str + 'A');
        Console.WriteLine(Math.Max(3, 9) + " " + Math.Abs(-2.5f) + " " + Math.Clamp(15, 0, 10));
        Console.WriteLine(Math.PI > 3.14f);
        Console.WriteLine(Gfx.Rgb(255, 0, 128) == 0xFF0080);
        bool ok = 3 > 2 || 1 / 0 == 0;
        Console.WriteLine(ok);
    }
}
