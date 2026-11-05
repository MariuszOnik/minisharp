class R { public int w = 2, h = 3; public R() => w = 4; public int Pole() => w * h; public void Rosnij() => h++; public override string ToString() => "R " + w + "x" + h; }
class P { static int Sq(int x) => x * x; static void Main() { var r = new R(); r.Rosnij(); Console.WriteLine(r.Pole() + " " + r + " " + Sq(5)); } }
