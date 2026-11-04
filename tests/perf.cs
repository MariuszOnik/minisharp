class P {
    static int Fib(int n) { if (n < 2) return n; return Fib(n - 1) + Fib(n - 2); }
    static void Main() {
        int t = Time.Millis();
        Console.WriteLine(Fib(30));
        int[] a = new int[100000];
        int s = 0;
        for (int r = 0; r < 50; r++)
            for (int i = 0; i < a.Length; i++) { a[i] = a[i] + i; }
        Console.WriteLine(a[99999]);
    }
}
