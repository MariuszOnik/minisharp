namespace Demo.Units {
    class Player {
        string name;
        int hp;

        public Player(string n, int h) { name = n; hp = h; }

        public void Hit(int dmg) { hp -= dmg; }

        public string Describe() { return name + " hp=" + hp; }
    }
}
