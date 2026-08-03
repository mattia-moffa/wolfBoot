/* Shims for the Shasta modules not compiled into wolfBoot. */

#include <string.h>
#include "max32665.h"
#include "gcr_regs.h"
#include "ZioAsyncIo.h"
#include "ZioDma.h"
#include "ZioHalClock.h"
#include "ZioHalMisc.h"
#include "ZioResult.h"
#include "ZioSerDebug.h"
#include "ZioTimer.h"
#include "ZioTypes.h"
#include "ZioUtils.h"

static eZioResult_T mLastResult = eZioResult_SUCCESS;
static eZioResult_T mCachedResult = eZioResult_SUCCESS;

eZioResult_T ZioResult_Set(eZioResult_T result, eZioSourceFile_T sourceFile, ZioInt16_T line,
                           ZioUint32_T value0, ZioUint32_T value1)
{
    (void)sourceFile;
    (void)line;
    (void)value0;
    (void)value1;
    if (result != eZioResult_SUCCESS)
        mLastResult = result;
    return result;
}

void ZioResult_Clear(void)
{
    mLastResult = eZioResult_SUCCESS;
}

void ZioResult_Cache(eZioResult_T currentResult)
{
    mCachedResult = currentResult;
}

eZioResult_T ZioResult_FirstFailurePrioritize(eZioResult_T originalResult, eZioResult_T newResult)
{
    return (originalResult != eZioResult_SUCCESS) ? originalResult : newResult;
}

void ZioSerDebug_printf(ZioUint8_T const *fmt, ...)
{
    (void)fmt;
}

eZioResult_T ZioTimer_WaitNap(ZioUint32_T timeoutMs)
{
    while (timeoutMs--)
        ZioUtils_SwDelayMs(1u);
    return eZioResult_SUCCESS;
}

eZioResult_T ZioDma_Init(void)
{
    return eZioResult_SUCCESS;
}

eZioResult_T ZioDma_Memcpy(void *dest, void *src, ZioUint16_T numBytes)
{
    memcpy(dest, src, numBytes);
    return eZioResult_SUCCESS;
}

eZioResult_T ZioDma_Qspi0RxIsBusy(eZioBool_T *pIsBusy_out)
{
    *pIsBusy_out = eZioBool_FALSE;
    return eZioResult_SUCCESS;
}

eZioResult_T ZioDma_Qspi0RxStart(void *rxBuf, ZioUint16_T numBytes, ZioAsyncIo_CompFuncPtr_T pCompFunc, void *pArg)
{
    (void)rxBuf;
    (void)numBytes;
    (void)pCompFunc;
    (void)pArg;
    return eZioResult_UNSPECIFIED_FAILURE;
}

eZioResult_T ZioDma_Qspi0RxStop(void)
{
    return eZioResult_SUCCESS;
}

eZioResult_T ZioDma_Qspi0TxStart(void *txBuf, ZioUint16_T numBytes, ZioAsyncIo_CompFuncPtr_T pCompFunc, void *pArg)
{
    (void)txBuf;
    (void)numBytes;
    (void)pCompFunc;
    (void)pArg;
    return eZioResult_UNSPECIFIED_FAILURE;
}

ZioUint32_T ZioHalMisc_InterruptCheck(void)
{
    return __get_PRIMASK();
}

void ZioHalMisc_InterruptDisable(void)
{
    __disable_irq();
}

void ZioHalMisc_InterruptEnable(void)
{
    __enable_irq();
}

void ZioHalClock_PeriphClockDisable(sys_periph_clock_t clockId)
{
    ZioUint32_T bitPos = (ZioUint32_T)clockId;
    if (bitPos > 31u) {
        bitPos -= 32u;
        MXC_GCR->perckcn1 |= ((ZioUint32_T)1u << bitPos);
    } else {
        MXC_GCR->perckcn0 |= ((ZioUint32_T)1u << bitPos);
    }
}

void ZioHalClock_PeriphClockEnable(sys_periph_clock_t clockId)
{
    ZioUint32_T bitPos = (ZioUint32_T)clockId;
    if (bitPos > 31u) {
        bitPos -= 32u;
        MXC_GCR->perckcn1 &= ~((ZioUint32_T)1u << bitPos);
    } else {
        MXC_GCR->perckcn0 &= ~((ZioUint32_T)1u << bitPos);
    }
}

#ifdef __ICCARM__
__weak void DMA1_IRQHandler(void)
{
}
#else
void __attribute__((weak)) DMA1_IRQHandler(void)
{
}
#endif
