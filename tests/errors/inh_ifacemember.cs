interface I { void F(); } class A : I { public void F() {} public int y; } class P { static void Main() { I i = new A(); int z = i.y; } }
