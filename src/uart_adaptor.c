
#include "main.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

extern UART_HandleTypeDef huart2;

static volatile uint8_t s_uart2_rx_byte;
static volatile uint16_t s_uart2_rx_head;
static volatile uint16_t s_uart2_rx_tail;
static uint8_t s_uart2_rx_buf[UART2_RX_BUFFER_SIZE];

static volatile bool s_uart2_locked;
static volatile bool s_uart2_tx_done;

static inline uint16_t uart2_rx_next(uint16_t index)
{
	return (uint16_t)((index + 1U) % UART2_RX_BUFFER_SIZE);
}

static void uart2_discard_all(void)
{
	__disable_irq();
	s_uart2_rx_tail = s_uart2_rx_head;
	__enable_irq();
}

bool UART2_TryLock(void)
{
	bool locked = false;
	__disable_irq();
	if (!s_uart2_locked)
	{
		s_uart2_locked = true;
		locked = true;
	}
	__enable_irq();
	return locked;
}

void UART2_Unlock(void)
{
	__disable_irq();
	s_uart2_locked = false;
	__enable_irq();
}

void UART2_RxStartIT(void)
{
	s_uart2_rx_head = 0U;
	s_uart2_rx_tail = 0U;
	(void)HAL_UART_Receive_IT(&huart2, (uint8_t *)&s_uart2_rx_byte, 1U);
}

HAL_StatusTypeDef UART2_SendBytes(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
	if ((data == NULL) || (len == 0U))
	{
		return HAL_OK;
	}
	return HAL_UART_Transmit(&huart2, (uint8_t *)data, len, timeout_ms);
}

HAL_StatusTypeDef UART2_SendBytesDMA(const uint8_t *data, uint16_t len)
{
	if ((data == NULL) || (len == 0U))
	{
		return HAL_OK;
	}

	s_uart2_tx_done = false;
	return HAL_UART_Transmit_DMA(&huart2, (uint8_t *)data, len);
}

bool UART2_TxDone(void)
{
	return s_uart2_tx_done;
}

void UART2_TxDoneClear(void)
{
	s_uart2_tx_done = false;
}

uint16_t UART2_Available(void)
{
	uint16_t head = s_uart2_rx_head;
	uint16_t tail = s_uart2_rx_tail;

	if (head >= tail)
	{
		return (uint16_t)(head - tail);
	}
	return (uint16_t)(UART2_RX_BUFFER_SIZE - (tail - head));
}

int UART2_ReadByte(uint8_t *out)
{
	if (out == NULL)
	{
		return 0;
	}
	if (s_uart2_rx_head == s_uart2_rx_tail)
	{
		return 0;
	}

	*out = s_uart2_rx_buf[s_uart2_rx_tail];
	s_uart2_rx_tail = uart2_rx_next(s_uart2_rx_tail);
	return 1;
}

uint16_t UART2_Read(uint8_t *dst, uint16_t len)
{
	if ((dst == NULL) || (len == 0U))
	{
		return 0U;
	}

	uint16_t read_count = 0U;
	while ((read_count < len) && (s_uart2_rx_head != s_uart2_rx_tail))
	{
		dst[read_count] = s_uart2_rx_buf[s_uart2_rx_tail];
		s_uart2_rx_tail = uart2_rx_next(s_uart2_rx_tail);
		read_count++;
	}
	return read_count;
}

void UART2_DiscardBuffered(void)
{
	uart2_discard_all();
}

bool UART2_ReadExactTimeout(uint8_t *dst, uint16_t len, uint32_t timeout_ms)
{
	if ((dst == NULL) || (len == 0U))
	{
		return true;
	}

	uint32_t const start_ms = HAL_GetTick();
	uint16_t got = 0U;

	while (got < len)
	{
		got += UART2_Read(&dst[got], (uint16_t)(len - got));
		if (got >= len)
		{
			return true;
		}
		if ((HAL_GetTick() - start_ms) >= timeout_ms)
		{
			return false;
		}
	}
	return true;
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
	if ((huart == NULL) || (huart->Instance != USART2))
	{
		return;
	}
	s_uart2_tx_done = true;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	if ((huart == NULL) || (huart->Instance != USART2))
	{
		return;
	}

	uint16_t next = uart2_rx_next(s_uart2_rx_head);
	if (next != s_uart2_rx_tail)
	{
		s_uart2_rx_buf[s_uart2_rx_head] = s_uart2_rx_byte;
		s_uart2_rx_head = next;
	}

	(void)HAL_UART_Receive_IT(huart, (uint8_t *)&s_uart2_rx_byte, 1U);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	if ((huart == NULL) || (huart->Instance != USART2))
	{
		return;
	}

	__HAL_UART_CLEAR_OREFLAG(huart);
	(void)HAL_UART_Receive_IT(huart, (uint8_t *)&s_uart2_rx_byte, 1U);
}

