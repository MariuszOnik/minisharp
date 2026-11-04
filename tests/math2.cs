// Math (like System.Math) and Mathf (like Unity). Floats are printed as RoundToInt(x * 1000).
class P {
    static int K(float x) { return Mathf.RoundToInt(x * 1000); }
    static void Main() {
        Console.WriteLine("round " + Math.Round(2.5f) + " " + Math.Round(3.5f) + " " + Math.Round(-2.5f) + " " + Mathf.RoundToInt(0.5f) + " " + Mathf.RoundToInt(1.5f));
        Console.WriteLine("trig " + K(Math.Asin(1)) + " " + K(Math.Acos(0)) + " " + K(Math.Atan(1)) + " " + K(Mathf.Atan2(1, 1)));
        Console.WriteLine("exp " + K(Math.Exp(1)) + " " + K(Math.Log(Math.E)) + " " + K(Math.Log10(1000)) + " " + K(Math.Truncate(-2.7f)));
        Console.WriteLine("sign " + Math.Sign(-5) + " " + Math.Sign(0) + " " + Math.Sign(3.5f) + " " + Mathf.Sign(0) + " " + Mathf.Sign(-0.1f));
        Console.WriteLine("toint " + Mathf.FloorToInt(-1.5f) + " " + Mathf.CeilToInt(-1.5f) + " " + Mathf.FloorToInt(2.9f) + " " + Mathf.CeilToInt(2.1f));
        Console.WriteLine("lerp " + K(Mathf.Lerp(10, 20, 0.25f)) + " " + K(Mathf.Lerp(10, 20, 2)) + " " + K(Mathf.LerpUnclamped(10, 20, 2)) + " " + K(Mathf.InverseLerp(10, 20, 15)) + " " + K(Mathf.InverseLerp(5, 5, 1)));
        Console.WriteLine("move " + K(Mathf.MoveTowards(0, 10, 3)) + " " + K(Mathf.MoveTowards(9, 10, 3)) + " " + K(Mathf.MoveTowards(5, -5, 2)));
        Console.WriteLine("smooth " + K(Mathf.SmoothStep(0, 1, 0.5f)) + " " + K(Mathf.SmoothStep(0, 10, 0.25f)) + " " + K(Mathf.Clamp01(1.7f)) + " " + K(Mathf.Clamp01(-1)));
        Console.WriteLine("repeat " + K(Mathf.Repeat(7.5f, 3)) + " " + K(Mathf.Repeat(-1, 3)) + " pingpong " + K(Mathf.PingPong(1.5f, 2)) + " " + K(Mathf.PingPong(3, 2)) + " " + K(Mathf.PingPong(5, 2)));
        Console.WriteLine("approx " + Mathf.Approximately(0.1f + 0.2f, 0.3f) + " " + Mathf.Approximately(1, 1.01f));
        Console.WriteLine("angle " + K(Mathf.DeltaAngle(350, 10)) + " " + K(Mathf.DeltaAngle(10, 350)) + " " + K(Mathf.DeltaAngle(0, 180)));
        Console.WriteLine("const " + K(Mathf.PI) + " " + K(Mathf.Deg2Rad * 180) + " " + K(Mathf.Rad2Deg) + " " + K(Math.E));
        Console.WriteLine("minmax " + Mathf.Max(3, 7) + " " + K(Mathf.Min(1.5f, -2)) + " " + Mathf.Clamp(15, 0, 10) + " " + Mathf.Abs(-4) + " " + K(Mathf.Ceil(1.2f)));
    }
}
