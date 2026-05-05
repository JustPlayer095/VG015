#include <stdio.h>
#include "calc.h"
#include "../../device/include/K1921VG015.h"
#include "../../device/include/plic.h"
#include "../../plib/inc/plib015_gpio.h"
#include "../gpio/gpio_helpers.h"
#include "../driver/ws0010/ws0010_1602.h"
#include "../timebase/timebase.h"

#define EXPR_MAX_LEN  64
#define MAX_TOKENS    64

typedef struct {
  char type;   // 'n' = number, 'o' = operator
  int  value;  // для числа
  char op;     // для оператора
} Token;

static char expr[EXPR_MAX_LEN];
static int  expr_len = 0;
static volatile uint32_t g_btn_last_ms[KEY_COUNT] = {0};
static volatile uint8_t g_btn_last_state[KEY_COUNT] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
};
static const uint32_t g_debounce_ms = 50;

static GPIO_TypeDef* const g_btn_ports[KEY_COUNT] = {
  GPIOB, GPIOB, GPIOB, GPIOB, GPIOB, GPIOB, GPIOB, GPIOB,
  GPIOB, GPIOB, GPIOB, GPIOB, GPIOB, GPIOB, GPIOB, GPIOB
};
static const uint32_t g_btn_pins[KEY_COUNT] = {
  GPIO_Pin_0,  GPIO_Pin_1,  GPIO_Pin_2,  GPIO_Pin_3,
  GPIO_Pin_4,  GPIO_Pin_5,  GPIO_Pin_6,  GPIO_Pin_7,
  GPIO_Pin_8,  GPIO_Pin_9,  GPIO_Pin_10, GPIO_Pin_11,
  GPIO_Pin_12, GPIO_Pin_13, GPIO_Pin_14, GPIO_Pin_15
};
static const key_id_t g_btn_keys[KEY_COUNT] = {
  KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7,
  KEY_8, KEY_9, KEY_PLUS, KEY_MINUS, KEY_MUL, KEY_DIV, KEY_DEL, KEY_EQ
};

static void gpio_irq_handler(void);

static void calc_gpio_init(void)
{
  RCU->CGCFGAHB_bit.GPIOBEN = 1;
  RCU->RSTDISAHB_bit.GPIOBEN = 1;
}

static void oled_show_expr_and_result(const char* expr_str, int has_result, int result)
{
  ws0010_clear();

  // Первая строка: выражение
  ws0010_goto(0, 0);
  if (expr_str && expr_str[0] != '\0') {
    ws0010_print(expr_str);
  }

  // Вторая строка: результат (или пасхалка при result == 1990)
  if (has_result) {
    ws0010_goto(1, 0);
    if (result == 1990) {
      // ЧЕРЕМША: Ч=0x06  Е→E  Р→P  Е→E  М→M  Ш=0x05  А→A
      static const char cheremsha[] = {
        '\x06', 'E', 'P', 'E', 'M', '\x05', 'A', '\0'
      };
      const char* p = cheremsha;
      while (*p) {
        ws0010_putc(*p++);
      }
    } else {
      char buf[17];
      snprintf(buf, sizeof(buf), "%d", result);
      ws0010_print(buf);
    }
  }
}

static void calc_gpio_irq_init(void)
{
  for (uint8_t i = 0; i < KEY_COUNT; i++) {
    gpio_init_input_irq(g_btn_ports[i], g_btn_pins[i], GPIO_PullMode_PU, &g_btn_last_state[i]);
  }

  PLIC_SetPriority(PLIC_GPIO_VECTNUM, 1);
  PLIC_SetIrqHandler(Plic_Mach_Target, PLIC_GPIO_VECTNUM, gpio_irq_handler);
  PLIC_IntEnable(Plic_Mach_Target, PLIC_GPIO_VECTNUM);
}

void calc_init(void)
{
  calc_gpio_init();
  timebase_init_1ms();
  calc_gpio_irq_init();
}

static void gpio_irq_handler(void)
{
  uint32_t now = ms_ticks;

  for (uint8_t i = 0; i < KEY_COUNT; i++) {
    if (GPIO_ITStatus(g_btn_ports[i], g_btn_pins[i]) == SET) {
      uint8_t btn_current = GPIO_ReadBit(g_btn_ports[i], g_btn_pins[i]) ? 1 : 0;

      if (g_btn_last_state[i] == 1 && btn_current == 0) {
        if ((now - g_btn_last_ms[i]) >= g_debounce_ms) {
          on_key_pressed(g_btn_keys[i]);
          g_btn_last_ms[i] = now;
          g_btn_last_state[i] = btn_current;
        }
      } else if (g_btn_last_state[i] == 0 && btn_current == 1) {
        if ((now - g_btn_last_ms[i]) >= g_debounce_ms) {
          g_btn_last_state[i] = btn_current;
          g_btn_last_ms[i] = now;
        }
      }

      GPIO_ITStatusClear(g_btn_ports[i], g_btn_pins[i]);
    }
  }
}

static int precedence(char op)
{
  if (op == '+' || op == '-') return 1;
  if (op == '*' || op == '/') return 2;
  return 0;
}

static int parse_to_rpn(const char *expr_str, Token *out, int *out_len)
{
  char op_stack[MAX_TOKENS];
  int  op_top = 0;
  int  i = 0;

  *out_len = 0;

  while (expr_str[i] != '\0') {
    char c = expr_str[i];

    if (c == ' ' || c == '\t') {
      i++;
      continue;
    }

    // Число (подряд идущие цифры)
    if (c >= '0' && c <= '9') {
      int value = 0;
      while (expr_str[i] >= '0' && expr_str[i] <= '9') {
        value = value * 10 + (expr_str[i] - '0');
        i++;
      }
      if (*out_len >= MAX_TOKENS) return -1;
      out[*out_len].type  = 'n';
      out[*out_len].value = value;
      (*out_len)++;
      continue;
    }

    // Оператор
    if (c == '+' || c == '-' || c == '*' || c == '/') {
      while (op_top > 0 &&
             precedence(op_stack[op_top - 1]) >= precedence(c)) {
        if (*out_len >= MAX_TOKENS) return -1;
        out[*out_len].type = 'o';
        out[*out_len].op   = op_stack[--op_top];
        (*out_len)++;
      }
      if (op_top >= MAX_TOKENS) return -2;
      op_stack[op_top++] = c;
      i++;
      continue;
    }

    // Неизвестный символ
    return -3;
  }

  // Выталкиваем оставшиеся операторы
  while (op_top > 0) {
    if (*out_len >= MAX_TOKENS) return -1;
    out[*out_len].type = 'o';
    out[*out_len].op   = op_stack[--op_top];
    (*out_len)++;
  }

  return 0;
}

static int eval_rpn(const Token *rpn, int len, int *result)
{
  int stack[MAX_TOKENS];
  int sp = 0;

  for (int i = 0; i < len; i++) {
    if (rpn[i].type == 'n') {
      if (sp >= MAX_TOKENS) return -1;
      stack[sp++] = rpn[i].value;
    } else if (rpn[i].type == 'o') {
      if (sp < 2) return -2;
      int b = stack[--sp];
      int a = stack[--sp];
      int r;

      switch (rpn[i].op) {
      case '+': r = a + b; break;
      case '-': r = a - b; break;
      case '*': r = a * b; break;
      case '/':
        if (b == 0) return -3;
        r = a / b;
        break;
      default:
        return -4;
      }

      stack[sp++] = r;
    } else {
      return -5;
    }
  }

  if (sp != 1) return -6;
  *result = stack[0];
  return 0;
}

void on_key_pressed(key_id_t key)
{
  switch (key) {
  case KEY_0: case KEY_1: case KEY_2: case KEY_3:
  case KEY_4: case KEY_5: case KEY_6: case KEY_7:
  case KEY_8: case KEY_9: {
    char c = '0' + (key - KEY_0);
    if (expr_len < EXPR_MAX_LEN - 1) {
      expr[expr_len++] = c;
      expr[expr_len] = '\0';
    }
    oled_show_expr_and_result(expr, 0, 0);
    break;
  }

  case KEY_PLUS: case KEY_MINUS: case KEY_MUL: case KEY_DIV: {
    // Нельзя ставить оператор первым символом
    if (expr_len == 0) {
      break;
    }

    // Нельзя ставить два оператора подряд
    char last = expr[expr_len - 1];
    if (last == '+' || last == '-' || last == '*' || last == '/') {
      break;
    }

    char c = 0;
    if (key == KEY_PLUS)  c = '+';
    if (key == KEY_MINUS) c = '-';
    if (key == KEY_MUL)   c = '*';
    if (key == KEY_DIV)   c = '/';

    if (expr_len < EXPR_MAX_LEN - 1) {
      expr[expr_len++] = c;
      expr[expr_len] = '\0';
    }
    oled_show_expr_and_result(expr, 0, 0);
    break;
  }

  case KEY_DEL:
    if (expr_len > 0) {
      expr[--expr_len] = '\0';
    }
    oled_show_expr_and_result(expr, 0, 0);
    break;

  case KEY_EQ: {
    if (expr_len == 0){
      break;
    }

    char last = expr[expr_len - 1];
    if (last == '+' || last == '-' || last == '*' || last == '/') {
        break;
    }

    Token rpn[MAX_TOKENS];
    int rpn_len;
    int rc = parse_to_rpn(expr, rpn, &rpn_len);
    if (rc != 0) {
      // Показываем только выражение, без результата
      oled_show_expr_and_result(expr, 0, 0);
      return;
    }

    int result;
    rc = eval_rpn(rpn, rpn_len, &result);
    if (rc != 0) {
      // Показываем только выражение, без результата
      oled_show_expr_and_result(expr, 0, 0);
      return;
    }

    // Показать результат под выражением
    oled_show_expr_and_result(expr, 1, result);

    // Очистить выражение после вычисления
    expr_len = 0;
    expr[0] = '\0';
    break;
  }

  default:
    break;
  }
}

