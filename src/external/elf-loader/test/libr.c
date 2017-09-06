__thread int g_b = 2;

void set_b (int b)
{
  g_b = b;
}

int get_b (void)
{
  return g_b;
}
