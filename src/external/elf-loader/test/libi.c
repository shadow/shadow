__thread int g_i[100000] = {1, 1};

int * get_i (void)
{
  return &g_i[0];
}
