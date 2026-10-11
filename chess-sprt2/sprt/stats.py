"""Statistics: pentanomial pair scoring, Elo estimate, and the SPRT.

Every opening is played twice (candidate as white, candidate as black). The two games
form a *pair* whose candidate score is 0, 0.5, 1, 1.5 or 2 points. The five counts
`penta = [n0, n05, n1, n15, n2]` are the sufficient statistics for everything below.
"""
import math

PENTA_SCORES = [0.0, 0.25, 0.5, 0.75, 1.0]  # pair points / 2
PENTA_LABELS = ["0", "0.5", "1", "1.5", "2"]


def score_to_elo(s):
    s = min(max(s, 1e-9), 1 - 1e-9)
    return -400.0 * math.log10(1.0 / s - 1.0)


def elo_to_score(elo):
    return 1.0 / (1.0 + 10.0 ** (-elo / 400.0))


def pair_bucket(points):
    """points: candidate points over a pair (0, .5, 1, 1.5, 2) -> bucket index 0..4."""
    return int(round(points * 2))


def _moments(counts, reg=0.0):
    w = [c + reg for c in counts]
    tot = sum(w)
    mean = sum(wi * s for wi, s in zip(w, PENTA_SCORES)) / tot
    var = sum(wi * (s - mean) ** 2 for wi, s in zip(w, PENTA_SCORES)) / tot
    return mean, var


def llr(counts, elo0, elo1, reg=0.25):
    """Normal-approximation pentanomial log-likelihood ratio of H1 (elo1) vs H0 (elo0).

    A small regularising pseudo-count keeps the variance from collapsing to zero
    in the first few pairs (e.g. all draws), which would otherwise give a huge LLR.
    """
    n = sum(counts)
    if n < 2:
        return 0.0
    mean, var = _moments(counts, reg)
    var = max(var, 1e-6)
    s0, s1 = elo_to_score(elo0), elo_to_score(elo1)
    return n * (s1 - s0) * (2 * mean - s0 - s1) / (2 * var)


def elo_estimate(counts):
    """Return dict(elo, margin, los) with a 95% interval; None values when too little data."""
    n = sum(counts)
    if n < 2:
        return {"elo": None, "margin": None, "los": None}
    mean, var = _moments(counts)
    se = math.sqrt(var / n)
    elo = score_to_elo(mean)
    if n < 10 or se == 0:
        margin = None
    else:
        lo = score_to_elo(mean - 1.96 * se)
        hi = score_to_elo(mean + 1.96 * se)
        margin = (hi - lo) / 2.0
    if se > 0:
        los = 0.5 * (1.0 + math.erf((mean - 0.5) / (se * math.sqrt(2.0))))
    else:
        los = 0.5 if mean == 0.5 else (1.0 if mean > 0.5 else 0.0)
    return {"elo": elo, "margin": margin, "los": los}


def sprt_bounds(alpha, beta):
    lower = math.log(beta / (1.0 - alpha))
    upper = math.log((1.0 - beta) / alpha)
    return lower, upper


def evaluate(counts, band_elo, alpha, beta, min_games):
    """Run both SPRTs and produce the four-way verdict.

    up   : H0 gain = 0   vs H1 gain = +band   (accepting H1 -> better)
    down : H0 gain = 0   vs H1 gain = -band   (accepting H1 -> worse)
    equal: both SPRTs accept H0 (the data prefers "no difference" over +-band)
    """
    lower, upper = sprt_bounds(alpha, beta)
    up = llr(counts, 0.0, band_elo)
    down = llr(counts, 0.0, -band_elo)
    games_in_pairs = 2 * sum(counts)
    verdict = "inconclusive"
    if games_in_pairs >= min_games:
        if down >= upper:
            verdict = "worse"
        elif up >= upper:
            verdict = "better"
        elif up <= lower and down <= lower:
            verdict = "equal"
    est = elo_estimate(counts)
    return {
        "verdict": verdict,
        "llr_up": up,
        "llr_down": down,
        "lower": lower,
        "upper": upper,
        **est,
    }
