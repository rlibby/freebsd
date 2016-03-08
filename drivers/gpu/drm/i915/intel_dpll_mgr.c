/*
 * Copyright © 2006-2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "intel_drv.h"

struct intel_shared_dpll *
intel_get_shared_dpll_by_id(struct drm_i915_private *dev_priv,
			    enum intel_dpll_id id)
{
	return &dev_priv->shared_dplls[id];
}

enum intel_dpll_id
intel_get_shared_dpll_id(struct drm_i915_private *dev_priv,
			 struct intel_shared_dpll *pll)
{
	if (WARN_ON(pll < dev_priv->shared_dplls||
		    pll > &dev_priv->shared_dplls[dev_priv->num_shared_dpll]))
		return -1;

	return (enum intel_dpll_id) (pll - dev_priv->shared_dplls);
}

void
intel_shared_dpll_config_get(struct intel_shared_dpll_config *config,
			     struct intel_shared_dpll *pll,
			     struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum intel_dpll_id id = intel_get_shared_dpll_id(dev_priv, pll);

	config[id].crtc_mask |= 1 << crtc->pipe;
}

void
intel_shared_dpll_config_put(struct intel_shared_dpll_config *config,
			     struct intel_shared_dpll *pll,
			     struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum intel_dpll_id id = intel_get_shared_dpll_id(dev_priv, pll);

	config[id].crtc_mask &= ~(1 << crtc->pipe);
}

/* For ILK+ */
void assert_shared_dpll(struct drm_i915_private *dev_priv,
			struct intel_shared_dpll *pll,
			bool state)
{
	bool cur_state;
	struct intel_dpll_hw_state hw_state;

	if (WARN(!pll, "asserting DPLL %s with no DPLL\n", onoff(state)))
		return;

	cur_state = pll->funcs.get_hw_state(dev_priv, pll, &hw_state);
	I915_STATE_WARN(cur_state != state,
	     "%s assertion failure (expected %s, current %s)\n",
			pll->name, onoff(state), onoff(cur_state));
}

void intel_prepare_shared_dpll(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_shared_dpll *pll = crtc->config->shared_dpll;

	if (WARN_ON(pll == NULL))
		return;

	WARN_ON(!pll->config.crtc_mask);
	if (pll->active == 0) {
		DRM_DEBUG_DRIVER("setting up %s\n", pll->name);
		WARN_ON(pll->on);
		assert_shared_dpll_disabled(dev_priv, pll);

		pll->funcs.mode_set(dev_priv, pll);
	}
}

/**
 * intel_enable_shared_dpll - enable PCH PLL
 * @dev_priv: i915 private structure
 * @pipe: pipe PLL to enable
 *
 * The PCH PLL needs to be enabled before the PCH transcoder, since it
 * drives the transcoder clock.
 */
void intel_enable_shared_dpll(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_shared_dpll *pll = crtc->config->shared_dpll;

	if (WARN_ON(pll == NULL))
		return;

	if (WARN_ON(pll->config.crtc_mask == 0))
		return;

	DRM_DEBUG_KMS("enable %s (active %d, on? %d) for crtc %d\n",
		      pll->name, pll->active, pll->on,
		      crtc->base.base.id);

	if (pll->active++) {
		WARN_ON(!pll->on);
		assert_shared_dpll_enabled(dev_priv, pll);
		return;
	}
	WARN_ON(pll->on);

	intel_display_power_get(dev_priv, POWER_DOMAIN_PLLS);

	DRM_DEBUG_KMS("enabling %s\n", pll->name);
	pll->funcs.enable(dev_priv, pll);
	pll->on = true;
}

void intel_disable_shared_dpll(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_shared_dpll *pll = crtc->config->shared_dpll;

	/* PCH only available on ILK+ */
	if (INTEL_INFO(dev)->gen < 5)
		return;

	if (pll == NULL)
		return;

	if (WARN_ON(!(pll->config.crtc_mask & (1 << drm_crtc_index(&crtc->base)))))
		return;

	DRM_DEBUG_KMS("disable %s (active %d, on? %d) for crtc %d\n",
		      pll->name, pll->active, pll->on,
		      crtc->base.base.id);

	if (WARN_ON(pll->active == 0)) {
		assert_shared_dpll_disabled(dev_priv, pll);
		return;
	}

	assert_shared_dpll_enabled(dev_priv, pll);
	WARN_ON(!pll->on);
	if (--pll->active)
		return;

	DRM_DEBUG_KMS("disabling %s\n", pll->name);
	pll->funcs.disable(dev_priv, pll);
	pll->on = false;

	intel_display_power_put(dev_priv, POWER_DOMAIN_PLLS);
}

static struct intel_shared_dpll *
intel_find_shared_dpll(struct intel_crtc *crtc,
		       struct intel_crtc_state *crtc_state,
		       enum intel_dpll_id range_min,
		       enum intel_dpll_id range_max)
{
	struct drm_i915_private *dev_priv = crtc->base.dev->dev_private;
	struct intel_shared_dpll *pll;
	struct intel_shared_dpll_config *shared_dpll;
	enum intel_dpll_id i;

	shared_dpll = intel_atomic_get_shared_dpll_state(crtc_state->base.state);

	for (i = range_min; i <= range_max; i++) {
		pll = &dev_priv->shared_dplls[i];

		/* Only want to check enabled timings first */
		if (shared_dpll[i].crtc_mask == 0)
			continue;

		if (memcmp(&crtc_state->dpll_hw_state,
			   &shared_dpll[i].hw_state,
			   sizeof(crtc_state->dpll_hw_state)) == 0) {
			DRM_DEBUG_KMS("CRTC:%d sharing existing %s (crtc mask 0x%08x, ative %d)\n",
				      crtc->base.base.id, pll->name,
				      shared_dpll[i].crtc_mask,
				      pll->active);
			return pll;
		}
	}

	/* Ok no matching timings, maybe there's a free one? */
	for (i = range_min; i <= range_max; i++) {
		pll = &dev_priv->shared_dplls[i];
		if (shared_dpll[i].crtc_mask == 0) {
			DRM_DEBUG_KMS("CRTC:%d allocated %s\n",
				      crtc->base.base.id, pll->name);
			return pll;
		}
	}

	return NULL;
}

static void
intel_reference_shared_dpll(struct intel_shared_dpll *pll,
			    struct intel_crtc_state *crtc_state)
{
	struct intel_shared_dpll_config *shared_dpll;
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->base.crtc);
	enum intel_dpll_id i = pll->id;

	shared_dpll = intel_atomic_get_shared_dpll_state(crtc_state->base.state);

	if (shared_dpll[i].crtc_mask == 0)
		shared_dpll[i].hw_state =
			crtc_state->dpll_hw_state;

	crtc_state->shared_dpll = pll;
	DRM_DEBUG_DRIVER("using %s for pipe %c\n", pll->name,
			 pipe_name(crtc->pipe));

	intel_shared_dpll_config_get(shared_dpll, pll, crtc);
}

void intel_shared_dpll_commit(struct drm_atomic_state *state)
{
	struct drm_i915_private *dev_priv = to_i915(state->dev);
	struct intel_shared_dpll_config *shared_dpll;
	struct intel_shared_dpll *pll;
	enum intel_dpll_id i;

	if (!to_intel_atomic_state(state)->dpll_set)
		return;

	shared_dpll = to_intel_atomic_state(state)->shared_dpll;
	for (i = 0; i < dev_priv->num_shared_dpll; i++) {
		pll = &dev_priv->shared_dplls[i];
		pll->config = shared_dpll[i];
	}
}

static bool ibx_pch_dpll_get_hw_state(struct drm_i915_private *dev_priv,
				      struct intel_shared_dpll *pll,
				      struct intel_dpll_hw_state *hw_state)
{
	uint32_t val;

	if (!intel_display_power_get_if_enabled(dev_priv, POWER_DOMAIN_PLLS))
		return false;

	val = I915_READ(PCH_DPLL(pll->id));
	hw_state->dpll = val;
	hw_state->fp0 = I915_READ(PCH_FP0(pll->id));
	hw_state->fp1 = I915_READ(PCH_FP1(pll->id));

	intel_display_power_put(dev_priv, POWER_DOMAIN_PLLS);

	return val & DPLL_VCO_ENABLE;
}

static void ibx_pch_dpll_mode_set(struct drm_i915_private *dev_priv,
				  struct intel_shared_dpll *pll)
{
	I915_WRITE(PCH_FP0(pll->id), pll->config.hw_state.fp0);
	I915_WRITE(PCH_FP1(pll->id), pll->config.hw_state.fp1);
}

static void ibx_assert_pch_refclk_enabled(struct drm_i915_private *dev_priv)
{
	u32 val;
	bool enabled;

	I915_STATE_WARN_ON(!(HAS_PCH_IBX(dev_priv->dev) || HAS_PCH_CPT(dev_priv->dev)));

	val = I915_READ(PCH_DREF_CONTROL);
	enabled = !!(val & (DREF_SSC_SOURCE_MASK | DREF_NONSPREAD_SOURCE_MASK |
			    DREF_SUPERSPREAD_SOURCE_MASK));
	I915_STATE_WARN(!enabled, "PCH refclk assertion failure, should be active but is disabled\n");
}

static void ibx_pch_dpll_enable(struct drm_i915_private *dev_priv,
				struct intel_shared_dpll *pll)
{
	/* PCH refclock must be enabled first */
	ibx_assert_pch_refclk_enabled(dev_priv);

	I915_WRITE(PCH_DPLL(pll->id), pll->config.hw_state.dpll);

	/* Wait for the clocks to stabilize. */
	POSTING_READ(PCH_DPLL(pll->id));
	udelay(150);

	/* The pixel multiplier can only be updated once the
	 * DPLL is enabled and the clocks are stable.
	 *
	 * So write it again.
	 */
	I915_WRITE(PCH_DPLL(pll->id), pll->config.hw_state.dpll);
	POSTING_READ(PCH_DPLL(pll->id));
	udelay(200);
}

static void ibx_pch_dpll_disable(struct drm_i915_private *dev_priv,
				 struct intel_shared_dpll *pll)
{
	struct drm_device *dev = dev_priv->dev;
	struct intel_crtc *crtc;

	/* Make sure no transcoder isn't still depending on us. */
	for_each_intel_crtc(dev, crtc) {
		if (crtc->config->shared_dpll == pll)
			assert_pch_transcoder_disabled(dev_priv, crtc->pipe);
	}

	I915_WRITE(PCH_DPLL(pll->id), 0);
	POSTING_READ(PCH_DPLL(pll->id));
	udelay(200);
}

static struct intel_shared_dpll *
ibx_get_dpll(struct intel_crtc *crtc, struct intel_crtc_state *crtc_state,
	     struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_shared_dpll *pll;
	enum intel_dpll_id i;

	if (HAS_PCH_IBX(dev_priv)) {
		/* Ironlake PCH has a fixed PLL->PCH pipe mapping. */
		i = (enum intel_dpll_id) crtc->pipe;
		pll = &dev_priv->shared_dplls[i];

		DRM_DEBUG_KMS("CRTC:%d using pre-allocated %s\n",
			      crtc->base.base.id, pll->name);
	} else {
		pll = intel_find_shared_dpll(crtc, crtc_state,
					     DPLL_ID_PCH_PLL_A,
					     DPLL_ID_PCH_PLL_B);
	}

	/* reference the pll */
	intel_reference_shared_dpll(pll, crtc_state);

	return pll;
}

static const struct intel_shared_dpll_funcs ibx_pch_dpll_funcs = {
	.mode_set = ibx_pch_dpll_mode_set,
	.enable = ibx_pch_dpll_enable,
	.disable = ibx_pch_dpll_disable,
	.get_hw_state = ibx_pch_dpll_get_hw_state,
};

static void hsw_ddi_wrpll_enable(struct drm_i915_private *dev_priv,
			       struct intel_shared_dpll *pll)
{
	I915_WRITE(WRPLL_CTL(pll->id), pll->config.hw_state.wrpll);
	POSTING_READ(WRPLL_CTL(pll->id));
	udelay(20);
}

static void hsw_ddi_spll_enable(struct drm_i915_private *dev_priv,
				struct intel_shared_dpll *pll)
{
	I915_WRITE(SPLL_CTL, pll->config.hw_state.spll);
	POSTING_READ(SPLL_CTL);
	udelay(20);
}

static void hsw_ddi_wrpll_disable(struct drm_i915_private *dev_priv,
				  struct intel_shared_dpll *pll)
{
	uint32_t val;

	val = I915_READ(WRPLL_CTL(pll->id));
	I915_WRITE(WRPLL_CTL(pll->id), val & ~WRPLL_PLL_ENABLE);
	POSTING_READ(WRPLL_CTL(pll->id));
}

static void hsw_ddi_spll_disable(struct drm_i915_private *dev_priv,
				 struct intel_shared_dpll *pll)
{
	uint32_t val;

	val = I915_READ(SPLL_CTL);
	I915_WRITE(SPLL_CTL, val & ~SPLL_PLL_ENABLE);
	POSTING_READ(SPLL_CTL);
}

static bool hsw_ddi_wrpll_get_hw_state(struct drm_i915_private *dev_priv,
				       struct intel_shared_dpll *pll,
				       struct intel_dpll_hw_state *hw_state)
{
	uint32_t val;

	if (!intel_display_power_get_if_enabled(dev_priv, POWER_DOMAIN_PLLS))
		return false;

	val = I915_READ(WRPLL_CTL(pll->id));
	hw_state->wrpll = val;

	intel_display_power_put(dev_priv, POWER_DOMAIN_PLLS);

	return val & WRPLL_PLL_ENABLE;
}

static bool hsw_ddi_spll_get_hw_state(struct drm_i915_private *dev_priv,
				      struct intel_shared_dpll *pll,
				      struct intel_dpll_hw_state *hw_state)
{
	uint32_t val;

	if (!intel_display_power_get_if_enabled(dev_priv, POWER_DOMAIN_PLLS))
		return false;

	val = I915_READ(SPLL_CTL);
	hw_state->spll = val;

	intel_display_power_put(dev_priv, POWER_DOMAIN_PLLS);

	return val & SPLL_PLL_ENABLE;
}

static uint32_t hsw_pll_to_ddi_pll_sel(struct intel_shared_dpll *pll)
{
	switch (pll->id) {
	case DPLL_ID_WRPLL1:
		return PORT_CLK_SEL_WRPLL1;
	case DPLL_ID_WRPLL2:
		return PORT_CLK_SEL_WRPLL2;
	case DPLL_ID_SPLL:
		return PORT_CLK_SEL_SPLL;
	default:
		return PORT_CLK_SEL_NONE;
	}
}

#define LC_FREQ 2700
#define LC_FREQ_2K U64_C(LC_FREQ * 2000)

#define P_MIN 2
#define P_MAX 64
#define P_INC 2

/* Constraints for PLL good behavior */
#define REF_MIN 48
#define REF_MAX 400
#define VCO_MIN 2400
#define VCO_MAX 4800

struct hsw_wrpll_rnp {
	unsigned p, n2, r2;
};

static unsigned hsw_wrpll_get_budget_for_freq(int clock)
{
	unsigned budget;

	switch (clock) {
	case 25175000:
	case 25200000:
	case 27000000:
	case 27027000:
	case 37762500:
	case 37800000:
	case 40500000:
	case 40541000:
	case 54000000:
	case 54054000:
	case 59341000:
	case 59400000:
	case 72000000:
	case 74176000:
	case 74250000:
	case 81000000:
	case 81081000:
	case 89012000:
	case 89100000:
	case 108000000:
	case 108108000:
	case 111264000:
	case 111375000:
	case 148352000:
	case 148500000:
	case 162000000:
	case 162162000:
	case 222525000:
	case 222750000:
	case 296703000:
	case 297000000:
		budget = 0;
		break;
	case 233500000:
	case 245250000:
	case 247750000:
	case 253250000:
	case 298000000:
		budget = 1500;
		break;
	case 169128000:
	case 169500000:
	case 179500000:
	case 202000000:
		budget = 2000;
		break;
	case 256250000:
	case 262500000:
	case 270000000:
	case 272500000:
	case 273750000:
	case 280750000:
	case 281250000:
	case 286000000:
	case 291750000:
		budget = 4000;
		break;
	case 267250000:
	case 268500000:
		budget = 5000;
		break;
	default:
		budget = 1000;
		break;
	}

	return budget;
}

static void hsw_wrpll_update_rnp(uint64_t freq2k, unsigned budget,
				 unsigned r2, unsigned n2, unsigned p,
				 struct hsw_wrpll_rnp *best)
{
	uint64_t a, b, c, d, diff, diff_best;

	/* No best (r,n,p) yet */
	if (best->p == 0) {
		best->p = p;
		best->n2 = n2;
		best->r2 = r2;
		return;
	}

	/*
	 * Output clock is (LC_FREQ_2K / 2000) * N / (P * R), which compares to
	 * freq2k.
	 *
	 * delta = 1e6 *
	 *	   abs(freq2k - (LC_FREQ_2K * n2/(p * r2))) /
	 *	   freq2k;
	 *
	 * and we would like delta <= budget.
	 *
	 * If the discrepancy is above the PPM-based budget, always prefer to
	 * improve upon the previous solution.  However, if you're within the
	 * budget, try to maximize Ref * VCO, that is N / (P * R^2).
	 */
	a = freq2k * budget * p * r2;
	b = freq2k * budget * best->p * best->r2;
	diff = abs_diff(freq2k * p * r2, LC_FREQ_2K * n2);
	diff_best = abs_diff(freq2k * best->p * best->r2,
			     LC_FREQ_2K * best->n2);
	c = 1000000 * diff;
	d = 1000000 * diff_best;

	if (a < c && b < d) {
		/* If both are above the budget, pick the closer */
		if (best->p * best->r2 * diff < p * r2 * diff_best) {
			best->p = p;
			best->n2 = n2;
			best->r2 = r2;
		}
	} else if (a >= c && b < d) {
		/* If A is below the threshold but B is above it?  Update. */
		best->p = p;
		best->n2 = n2;
		best->r2 = r2;
	} else if (a >= c && b >= d) {
		/* Both are below the limit, so pick the higher n2/(r2*r2) */
		if (n2 * best->r2 * best->r2 > best->n2 * r2 * r2) {
			best->p = p;
			best->n2 = n2;
			best->r2 = r2;
		}
	}
	/* Otherwise a < c && b >= d, do nothing */
}

static void
hsw_ddi_calculate_wrpll(int clock /* in Hz */,
			unsigned *r2_out, unsigned *n2_out, unsigned *p_out)
{
	uint64_t freq2k;
	unsigned p, n2, r2;
	struct hsw_wrpll_rnp best = { 0, 0, 0 };
	unsigned budget;

	freq2k = clock / 100;

	budget = hsw_wrpll_get_budget_for_freq(clock);

	/* Special case handling for 540 pixel clock: bypass WR PLL entirely
	 * and directly pass the LC PLL to it. */
	if (freq2k == 5400000) {
		*n2_out = 2;
		*p_out = 1;
		*r2_out = 2;
		return;
	}

	/*
	 * Ref = LC_FREQ / R, where Ref is the actual reference input seen by
	 * the WR PLL.
	 *
	 * We want R so that REF_MIN <= Ref <= REF_MAX.
	 * Injecting R2 = 2 * R gives:
	 *   REF_MAX * r2 > LC_FREQ * 2 and
	 *   REF_MIN * r2 < LC_FREQ * 2
	 *
	 * Which means the desired boundaries for r2 are:
	 *  LC_FREQ * 2 / REF_MAX < r2 < LC_FREQ * 2 / REF_MIN
	 *
	 */
	for (r2 = LC_FREQ * 2 / REF_MAX + 1;
	     r2 <= LC_FREQ * 2 / REF_MIN;
	     r2++) {

		/*
		 * VCO = N * Ref, that is: VCO = N * LC_FREQ / R
		 *
		 * Once again we want VCO_MIN <= VCO <= VCO_MAX.
		 * Injecting R2 = 2 * R and N2 = 2 * N, we get:
		 *   VCO_MAX * r2 > n2 * LC_FREQ and
		 *   VCO_MIN * r2 < n2 * LC_FREQ)
		 *
		 * Which means the desired boundaries for n2 are:
		 * VCO_MIN * r2 / LC_FREQ < n2 < VCO_MAX * r2 / LC_FREQ
		 */
		for (n2 = VCO_MIN * r2 / LC_FREQ + 1;
		     n2 <= VCO_MAX * r2 / LC_FREQ;
		     n2++) {

			for (p = P_MIN; p <= P_MAX; p += P_INC)
				hsw_wrpll_update_rnp(freq2k, budget,
						     r2, n2, p, &best);
		}
	}

	*n2_out = best.n2;
	*p_out = best.p;
	*r2_out = best.r2;
}

static struct intel_shared_dpll *
hsw_get_dpll(struct intel_crtc *crtc, struct intel_crtc_state *crtc_state,
	     struct intel_encoder *encoder)
{
	struct intel_shared_dpll *pll;
	int clock = crtc_state->port_clock;

	if (encoder->type == INTEL_OUTPUT_HDMI) {
		uint32_t val;
		unsigned p, n2, r2;

		hsw_ddi_calculate_wrpll(clock * 1000, &r2, &n2, &p);

		val = WRPLL_PLL_ENABLE | WRPLL_PLL_LCPLL |
		      WRPLL_DIVIDER_REFERENCE(r2) | WRPLL_DIVIDER_FEEDBACK(n2) |
		      WRPLL_DIVIDER_POST(p);

		memset(&crtc_state->dpll_hw_state, 0,
		       sizeof(crtc_state->dpll_hw_state));

		crtc_state->dpll_hw_state.wrpll = val;

		pll = intel_find_shared_dpll(crtc, crtc_state,
					     DPLL_ID_WRPLL1, DPLL_ID_WRPLL2);

	} else if (encoder->type == INTEL_OUTPUT_ANALOG) {
		if (WARN_ON(crtc_state->port_clock / 2 != 135000))
			return NULL;

		memset(&crtc_state->dpll_hw_state, 0,
		       sizeof(crtc_state->dpll_hw_state));

		crtc_state->dpll_hw_state.spll =
			SPLL_PLL_ENABLE | SPLL_PLL_FREQ_1350MHz | SPLL_PLL_SSC;

		pll = intel_find_shared_dpll(crtc, crtc_state,
					     DPLL_ID_SPLL, DPLL_ID_SPLL);
	} else {
		return NULL;
	}

	if (!pll)
		return NULL;

	crtc_state->ddi_pll_sel = hsw_pll_to_ddi_pll_sel(pll);

	intel_reference_shared_dpll(pll, crtc_state);

	return pll;
}


static const struct intel_shared_dpll_funcs hsw_ddi_wrpll_funcs = {
	.enable = hsw_ddi_wrpll_enable,
	.disable = hsw_ddi_wrpll_disable,
	.get_hw_state = hsw_ddi_wrpll_get_hw_state,
};

static const struct intel_shared_dpll_funcs hsw_ddi_spll_funcs = {
	.enable = hsw_ddi_spll_enable,
	.disable = hsw_ddi_spll_disable,
	.get_hw_state = hsw_ddi_spll_get_hw_state,
};

struct skl_dpll_regs {
	i915_reg_t ctl, cfgcr1, cfgcr2;
};

/* this array is indexed by the *shared* pll id */
static const struct skl_dpll_regs skl_dpll_regs[3] = {
	{
		/* DPLL 1 */
		.ctl = LCPLL2_CTL,
		.cfgcr1 = DPLL_CFGCR1(SKL_DPLL1),
		.cfgcr2 = DPLL_CFGCR2(SKL_DPLL1),
	},
	{
		/* DPLL 2 */
		.ctl = WRPLL_CTL(0),
		.cfgcr1 = DPLL_CFGCR1(SKL_DPLL2),
		.cfgcr2 = DPLL_CFGCR2(SKL_DPLL2),
	},
	{
		/* DPLL 3 */
		.ctl = WRPLL_CTL(1),
		.cfgcr1 = DPLL_CFGCR1(SKL_DPLL3),
		.cfgcr2 = DPLL_CFGCR2(SKL_DPLL3),
	},
};

static void skl_ddi_pll_enable(struct drm_i915_private *dev_priv,
			       struct intel_shared_dpll *pll)
{
	uint32_t val;
	unsigned int dpll;
	const struct skl_dpll_regs *regs = skl_dpll_regs;

	/* DPLL0 is not part of the shared DPLLs, so pll->id is 0 for DPLL1 */
	dpll = pll->id + 1;

	val = I915_READ(DPLL_CTRL1);

	val &= ~(DPLL_CTRL1_HDMI_MODE(dpll) | DPLL_CTRL1_SSC(dpll) |
		 DPLL_CTRL1_LINK_RATE_MASK(dpll));
	val |= pll->config.hw_state.ctrl1 << (dpll * 6);

	I915_WRITE(DPLL_CTRL1, val);
	POSTING_READ(DPLL_CTRL1);

	I915_WRITE(regs[pll->id].cfgcr1, pll->config.hw_state.cfgcr1);
	I915_WRITE(regs[pll->id].cfgcr2, pll->config.hw_state.cfgcr2);
	POSTING_READ(regs[pll->id].cfgcr1);
	POSTING_READ(regs[pll->id].cfgcr2);

	/* the enable bit is always bit 31 */
	I915_WRITE(regs[pll->id].ctl,
		   I915_READ(regs[pll->id].ctl) | LCPLL_PLL_ENABLE);

	if (wait_for(I915_READ(DPLL_STATUS) & DPLL_LOCK(dpll), 5))
		DRM_ERROR("DPLL %d not locked\n", dpll);
}

static void skl_ddi_pll_disable(struct drm_i915_private *dev_priv,
				struct intel_shared_dpll *pll)
{
	const struct skl_dpll_regs *regs = skl_dpll_regs;

	/* the enable bit is always bit 31 */
	I915_WRITE(regs[pll->id].ctl,
		   I915_READ(regs[pll->id].ctl) & ~LCPLL_PLL_ENABLE);
	POSTING_READ(regs[pll->id].ctl);
}

static bool skl_ddi_pll_get_hw_state(struct drm_i915_private *dev_priv,
				     struct intel_shared_dpll *pll,
				     struct intel_dpll_hw_state *hw_state)
{
	uint32_t val;
	unsigned int dpll;
	const struct skl_dpll_regs *regs = skl_dpll_regs;
	bool ret;

	if (!intel_display_power_get_if_enabled(dev_priv, POWER_DOMAIN_PLLS))
		return false;

	ret = false;

	/* DPLL0 is not part of the shared DPLLs, so pll->id is 0 for DPLL1 */
	dpll = pll->id + 1;

	val = I915_READ(regs[pll->id].ctl);
	if (!(val & LCPLL_PLL_ENABLE))
		goto out;

	val = I915_READ(DPLL_CTRL1);
	hw_state->ctrl1 = (val >> (dpll * 6)) & 0x3f;

	/* avoid reading back stale values if HDMI mode is not enabled */
	if (val & DPLL_CTRL1_HDMI_MODE(dpll)) {
		hw_state->cfgcr1 = I915_READ(regs[pll->id].cfgcr1);
		hw_state->cfgcr2 = I915_READ(regs[pll->id].cfgcr2);
	}
	ret = true;

out:
	intel_display_power_put(dev_priv, POWER_DOMAIN_PLLS);

	return ret;
}

static struct intel_shared_dpll *
skl_get_dpll(struct intel_crtc *crtc, struct intel_crtc_state *crtc_state,
	     struct intel_encoder *encoder)
{
	struct intel_shared_dpll *pll;

	pll = intel_find_shared_dpll(crtc, crtc_state,
				     DPLL_ID_SKL_DPLL1, DPLL_ID_SKL_DPLL3);
	if (pll)
		intel_reference_shared_dpll(pll, crtc_state);

	return pll;
}

static const struct intel_shared_dpll_funcs skl_ddi_pll_funcs = {
	.enable = skl_ddi_pll_enable,
	.disable = skl_ddi_pll_disable,
	.get_hw_state = skl_ddi_pll_get_hw_state,
};

static void bxt_ddi_pll_enable(struct drm_i915_private *dev_priv,
				struct intel_shared_dpll *pll)
{
	uint32_t temp;
	enum port port = (enum port)pll->id;	/* 1:1 port->PLL mapping */

	temp = I915_READ(BXT_PORT_PLL_ENABLE(port));
	temp &= ~PORT_PLL_REF_SEL;
	/* Non-SSC reference */
	I915_WRITE(BXT_PORT_PLL_ENABLE(port), temp);

	/* Disable 10 bit clock */
	temp = I915_READ(BXT_PORT_PLL_EBB_4(port));
	temp &= ~PORT_PLL_10BIT_CLK_ENABLE;
	I915_WRITE(BXT_PORT_PLL_EBB_4(port), temp);

	/* Write P1 & P2 */
	temp = I915_READ(BXT_PORT_PLL_EBB_0(port));
	temp &= ~(PORT_PLL_P1_MASK | PORT_PLL_P2_MASK);
	temp |= pll->config.hw_state.ebb0;
	I915_WRITE(BXT_PORT_PLL_EBB_0(port), temp);

	/* Write M2 integer */
	temp = I915_READ(BXT_PORT_PLL(port, 0));
	temp &= ~PORT_PLL_M2_MASK;
	temp |= pll->config.hw_state.pll0;
	I915_WRITE(BXT_PORT_PLL(port, 0), temp);

	/* Write N */
	temp = I915_READ(BXT_PORT_PLL(port, 1));
	temp &= ~PORT_PLL_N_MASK;
	temp |= pll->config.hw_state.pll1;
	I915_WRITE(BXT_PORT_PLL(port, 1), temp);

	/* Write M2 fraction */
	temp = I915_READ(BXT_PORT_PLL(port, 2));
	temp &= ~PORT_PLL_M2_FRAC_MASK;
	temp |= pll->config.hw_state.pll2;
	I915_WRITE(BXT_PORT_PLL(port, 2), temp);

	/* Write M2 fraction enable */
	temp = I915_READ(BXT_PORT_PLL(port, 3));
	temp &= ~PORT_PLL_M2_FRAC_ENABLE;
	temp |= pll->config.hw_state.pll3;
	I915_WRITE(BXT_PORT_PLL(port, 3), temp);

	/* Write coeff */
	temp = I915_READ(BXT_PORT_PLL(port, 6));
	temp &= ~PORT_PLL_PROP_COEFF_MASK;
	temp &= ~PORT_PLL_INT_COEFF_MASK;
	temp &= ~PORT_PLL_GAIN_CTL_MASK;
	temp |= pll->config.hw_state.pll6;
	I915_WRITE(BXT_PORT_PLL(port, 6), temp);

	/* Write calibration val */
	temp = I915_READ(BXT_PORT_PLL(port, 8));
	temp &= ~PORT_PLL_TARGET_CNT_MASK;
	temp |= pll->config.hw_state.pll8;
	I915_WRITE(BXT_PORT_PLL(port, 8), temp);

	temp = I915_READ(BXT_PORT_PLL(port, 9));
	temp &= ~PORT_PLL_LOCK_THRESHOLD_MASK;
	temp |= pll->config.hw_state.pll9;
	I915_WRITE(BXT_PORT_PLL(port, 9), temp);

	temp = I915_READ(BXT_PORT_PLL(port, 10));
	temp &= ~PORT_PLL_DCO_AMP_OVR_EN_H;
	temp &= ~PORT_PLL_DCO_AMP_MASK;
	temp |= pll->config.hw_state.pll10;
	I915_WRITE(BXT_PORT_PLL(port, 10), temp);

	/* Recalibrate with new settings */
	temp = I915_READ(BXT_PORT_PLL_EBB_4(port));
	temp |= PORT_PLL_RECALIBRATE;
	I915_WRITE(BXT_PORT_PLL_EBB_4(port), temp);
	temp &= ~PORT_PLL_10BIT_CLK_ENABLE;
	temp |= pll->config.hw_state.ebb4;
	I915_WRITE(BXT_PORT_PLL_EBB_4(port), temp);

	/* Enable PLL */
	temp = I915_READ(BXT_PORT_PLL_ENABLE(port));
	temp |= PORT_PLL_ENABLE;
	I915_WRITE(BXT_PORT_PLL_ENABLE(port), temp);
	POSTING_READ(BXT_PORT_PLL_ENABLE(port));

	if (wait_for_atomic_us((I915_READ(BXT_PORT_PLL_ENABLE(port)) &
			PORT_PLL_LOCK), 200))
		DRM_ERROR("PLL %d not locked\n", port);

	/*
	 * While we write to the group register to program all lanes at once we
	 * can read only lane registers and we pick lanes 0/1 for that.
	 */
	temp = I915_READ(BXT_PORT_PCS_DW12_LN01(port));
	temp &= ~LANE_STAGGER_MASK;
	temp &= ~LANESTAGGER_STRAP_OVRD;
	temp |= pll->config.hw_state.pcsdw12;
	I915_WRITE(BXT_PORT_PCS_DW12_GRP(port), temp);
}

static void bxt_ddi_pll_disable(struct drm_i915_private *dev_priv,
					struct intel_shared_dpll *pll)
{
	enum port port = (enum port)pll->id;	/* 1:1 port->PLL mapping */
	uint32_t temp;

	temp = I915_READ(BXT_PORT_PLL_ENABLE(port));
	temp &= ~PORT_PLL_ENABLE;
	I915_WRITE(BXT_PORT_PLL_ENABLE(port), temp);
	POSTING_READ(BXT_PORT_PLL_ENABLE(port));
}

static bool bxt_ddi_pll_get_hw_state(struct drm_i915_private *dev_priv,
					struct intel_shared_dpll *pll,
					struct intel_dpll_hw_state *hw_state)
{
	enum port port = (enum port)pll->id;	/* 1:1 port->PLL mapping */
	uint32_t val;
	bool ret;

	if (!intel_display_power_get_if_enabled(dev_priv, POWER_DOMAIN_PLLS))
		return false;

	ret = false;

	val = I915_READ(BXT_PORT_PLL_ENABLE(port));
	if (!(val & PORT_PLL_ENABLE))
		goto out;

	hw_state->ebb0 = I915_READ(BXT_PORT_PLL_EBB_0(port));
	hw_state->ebb0 &= PORT_PLL_P1_MASK | PORT_PLL_P2_MASK;

	hw_state->ebb4 = I915_READ(BXT_PORT_PLL_EBB_4(port));
	hw_state->ebb4 &= PORT_PLL_10BIT_CLK_ENABLE;

	hw_state->pll0 = I915_READ(BXT_PORT_PLL(port, 0));
	hw_state->pll0 &= PORT_PLL_M2_MASK;

	hw_state->pll1 = I915_READ(BXT_PORT_PLL(port, 1));
	hw_state->pll1 &= PORT_PLL_N_MASK;

	hw_state->pll2 = I915_READ(BXT_PORT_PLL(port, 2));
	hw_state->pll2 &= PORT_PLL_M2_FRAC_MASK;

	hw_state->pll3 = I915_READ(BXT_PORT_PLL(port, 3));
	hw_state->pll3 &= PORT_PLL_M2_FRAC_ENABLE;

	hw_state->pll6 = I915_READ(BXT_PORT_PLL(port, 6));
	hw_state->pll6 &= PORT_PLL_PROP_COEFF_MASK |
			  PORT_PLL_INT_COEFF_MASK |
			  PORT_PLL_GAIN_CTL_MASK;

	hw_state->pll8 = I915_READ(BXT_PORT_PLL(port, 8));
	hw_state->pll8 &= PORT_PLL_TARGET_CNT_MASK;

	hw_state->pll9 = I915_READ(BXT_PORT_PLL(port, 9));
	hw_state->pll9 &= PORT_PLL_LOCK_THRESHOLD_MASK;

	hw_state->pll10 = I915_READ(BXT_PORT_PLL(port, 10));
	hw_state->pll10 &= PORT_PLL_DCO_AMP_OVR_EN_H |
			   PORT_PLL_DCO_AMP_MASK;

	/*
	 * While we write to the group register to program all lanes at once we
	 * can read only lane registers. We configure all lanes the same way, so
	 * here just read out lanes 0/1 and output a note if lanes 2/3 differ.
	 */
	hw_state->pcsdw12 = I915_READ(BXT_PORT_PCS_DW12_LN01(port));
	if (I915_READ(BXT_PORT_PCS_DW12_LN23(port)) != hw_state->pcsdw12)
		DRM_DEBUG_DRIVER("lane stagger config different for lane 01 (%08x) and 23 (%08x)\n",
				 hw_state->pcsdw12,
				 I915_READ(BXT_PORT_PCS_DW12_LN23(port)));
	hw_state->pcsdw12 &= LANE_STAGGER_MASK | LANESTAGGER_STRAP_OVRD;

	ret = true;

out:
	intel_display_power_put(dev_priv, POWER_DOMAIN_PLLS);

	return ret;
}

static struct intel_shared_dpll *
bxt_get_dpll(struct intel_crtc *crtc, struct intel_crtc_state *crtc_state,
	     struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_digital_port *intel_dig_port;
	struct intel_shared_dpll *pll;
	enum intel_dpll_id i;

	/* PLL is attached to port in bxt */
	encoder = intel_ddi_get_crtc_new_encoder(crtc_state);
	if (WARN_ON(!encoder))
		return NULL;

	intel_dig_port = enc_to_dig_port(&encoder->base);
	/* 1:1 mapping between ports and PLLs */
	i = (enum intel_dpll_id)intel_dig_port->port;
	pll = &dev_priv->shared_dplls[i];
	DRM_DEBUG_KMS("CRTC:%d using pre-allocated %s\n",
		crtc->base.base.id, pll->name);

	intel_reference_shared_dpll(pll, crtc_state);

	return pll;
}

static const struct intel_shared_dpll_funcs bxt_ddi_pll_funcs = {
	.enable = bxt_ddi_pll_enable,
	.disable = bxt_ddi_pll_disable,
	.get_hw_state = bxt_ddi_pll_get_hw_state,
};

static void intel_ddi_pll_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	uint32_t val = I915_READ(LCPLL_CTL);

	if (IS_SKYLAKE(dev) || IS_KABYLAKE(dev)) {
		int cdclk_freq;

		cdclk_freq = dev_priv->display.get_display_clock_speed(dev);
		dev_priv->skl_boot_cdclk = cdclk_freq;
		if (skl_sanitize_cdclk(dev_priv))
			DRM_DEBUG_KMS("Sanitized cdclk programmed by pre-os\n");
		if (!(I915_READ(LCPLL1_CTL) & LCPLL_PLL_ENABLE))
			DRM_ERROR("LCPLL1 is disabled\n");
	} else if (IS_BROXTON(dev)) {
		broxton_init_cdclk(dev);
		broxton_ddi_phy_init(dev);
	} else {
		/*
		 * The LCPLL register should be turned on by the BIOS. For now
		 * let's just check its state and print errors in case
		 * something is wrong.  Don't even try to turn it on.
		 */

		if (val & LCPLL_CD_SOURCE_FCLK)
			DRM_ERROR("CDCLK source is not LCPLL\n");

		if (val & LCPLL_PLL_DISABLE)
			DRM_ERROR("LCPLL is disabled\n");
	}
}

struct dpll_info {
	const char *name;
	const int id;
	const struct intel_shared_dpll_funcs *funcs;
};

struct intel_dpll_mgr {
	const struct dpll_info *dpll_info;

	struct intel_shared_dpll *(*get_dpll)(struct intel_crtc *crtc,
					      struct intel_crtc_state *crtc_state,
					      struct intel_encoder *encoder);
};

static const struct dpll_info pch_plls[] = {
	{ "PCH DPLL A", DPLL_ID_PCH_PLL_A, &ibx_pch_dpll_funcs },
	{ "PCH DPLL B", DPLL_ID_PCH_PLL_B, &ibx_pch_dpll_funcs },
	{ NULL, -1, NULL },
};

static const struct intel_dpll_mgr pch_pll_mgr = {
	.dpll_info = pch_plls,
	.get_dpll = ibx_get_dpll,
};

static const struct dpll_info hsw_plls[] = {
	{ "WRPLL 1", DPLL_ID_WRPLL1, &hsw_ddi_wrpll_funcs },
	{ "WRPLL 2", DPLL_ID_WRPLL2, &hsw_ddi_wrpll_funcs },
	{ "SPLL",    DPLL_ID_SPLL,   &hsw_ddi_spll_funcs },
	{ NULL, -1, NULL, },
};

static const struct intel_dpll_mgr hsw_pll_mgr = {
	.dpll_info = hsw_plls,
	.get_dpll = hsw_get_dpll,
};

static const struct dpll_info skl_plls[] = {
	{ "DPPL 1", DPLL_ID_SKL_DPLL1, &skl_ddi_pll_funcs },
	{ "DPPL 2", DPLL_ID_SKL_DPLL2, &skl_ddi_pll_funcs },
	{ "DPPL 3", DPLL_ID_SKL_DPLL3, &skl_ddi_pll_funcs },
	{ NULL, -1, NULL, },
};

static const struct intel_dpll_mgr skl_pll_mgr = {
	.dpll_info = skl_plls,
	.get_dpll = skl_get_dpll,
};

static const struct dpll_info bxt_plls[] = {
	{ "PORT PLL A", 0, &bxt_ddi_pll_funcs },
	{ "PORT PLL B", 1, &bxt_ddi_pll_funcs },
	{ "PORT PLL C", 2, &bxt_ddi_pll_funcs },
	{ NULL, -1, NULL, },
};

static const struct intel_dpll_mgr bxt_pll_mgr = {
	.dpll_info = bxt_plls,
	.get_dpll = bxt_get_dpll,
};

void intel_shared_dpll_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	const struct intel_dpll_mgr *dpll_mgr = NULL;
	const struct dpll_info *dpll_info;
	int i;

	if (IS_SKYLAKE(dev) || IS_KABYLAKE(dev))
		dpll_mgr = &skl_pll_mgr;
	else if IS_BROXTON(dev)
		dpll_mgr = &bxt_pll_mgr;
	else if (HAS_DDI(dev))
		dpll_mgr = &hsw_pll_mgr;
	else if (HAS_PCH_IBX(dev) || HAS_PCH_CPT(dev))
		dpll_mgr = &pch_pll_mgr;

	if (!dpll_mgr) {
		dev_priv->num_shared_dpll = 0;
		return;
	}

	dpll_info = dpll_mgr->dpll_info;

	for (i = 0; dpll_info[i].id >= 0; i++) {
		WARN_ON(i != dpll_info[i].id);

		dev_priv->shared_dplls[i].id = dpll_info[i].id;
		dev_priv->shared_dplls[i].name = dpll_info[i].name;
		dev_priv->shared_dplls[i].funcs = *dpll_info[i].funcs;
	}

	dev_priv->dpll_mgr = dpll_mgr;
	dev_priv->num_shared_dpll = i;

	BUG_ON(dev_priv->num_shared_dpll > I915_NUM_PLLS);

	/* FIXME: Move this to a more suitable place */
	if (HAS_DDI(dev))
		intel_ddi_pll_init(dev);
}

struct intel_shared_dpll *
intel_get_shared_dpll(struct intel_crtc *crtc,
		      struct intel_crtc_state *crtc_state,
		      struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct intel_dpll_mgr *dpll_mgr = dev_priv->dpll_mgr;

	if (WARN_ON(!dpll_mgr))
		return NULL;

	return dpll_mgr->get_dpll(crtc, crtc_state, encoder);
}
