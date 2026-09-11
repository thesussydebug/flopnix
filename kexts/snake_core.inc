/* Updates the snake, food, and collisions. */
#define SNAKE_MAX_MARGIN   3
#define SNAKE_MARGIN_EVERY 5

static int snake_food_margin(int score)
{
    if (score <= 0) return 0;
    int m = score / SNAKE_MARGIN_EVERY;
    return m > SNAKE_MAX_MARGIN ? SNAKE_MAX_MARGIN : m;
}

static int snake_food_box(int score, int gw, int gh,
                          int *x0, int *y0, int *x1, int *y1)
{
    int m = snake_food_margin(score);
    while (m > 0 && (gw - 2 * m < 2 || gh - 2 * m < 2)) m--;
    *x0 = m;
    *y0 = m;
    *x1 = gw - m;
    *y1 = gh - m;
    return m;
}
