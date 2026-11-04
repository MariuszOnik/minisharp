// Images and sounds on the PC host (paths are relative to this file's folder).
class P {
    static void Main() {
        int hero = Gfx.LoadImage("../examples/hero.png");
        int again = Gfx.LoadImage("../examples/hero.png");
        int coin = Gfx.LoadImage("../examples/coin.png");
        Console.WriteLine("hero " + hero + " again " + again + " coin " + coin);
        Console.WriteLine("size " + Gfx.ImageWidth(hero) + "x" + Gfx.ImageHeight(hero) + " coin " + Gfx.ImageWidth(coin) + "x" + Gfx.ImageHeight(coin));
        Gfx.Image(hero, 10, 10);
        Gfx.Image(hero, 10, 10, 2.5f);
        Gfx.ImageEx(coin, 50, 50, -2, 2, 45);
        Gfx.ImagePart(hero, 0, 0, 48, 0, 16, 16);
        Gfx.ImagePartEx(hero, 0, 0, 16, 0, 16, 16, -3, 3, 90);
        int snd = Audio.Load("../examples/coin.wav");
        Audio.Play(snd); Audio.Play(snd, 0.5f); Audio.PlayLoop(snd); Audio.Stop(snd); Audio.StopAll();
        Console.WriteLine("sound " + snd + " ok");
    }
}
