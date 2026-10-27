if (card.contains(u"buydown"_s) || card.contains(u"buydownGroup"_s) || card.contains(u"buydownStart"_s) || card.contains(u"buydownEnd"_s)) {
    auto parseYmd = [](const QString &s) -> double {
        QString d; for (QChar c : s.trimmed()) if (c.isDigit()) d.append(c);
        return d.size() == 8 ? d.toDouble() : 0.0;
    };
    const double bdAmt = std::max(0.0, card.value(u"buydown"_s).toString().trimmed().toDouble());
    const QString bdGrp = card.value(u"buydownGroup"_s).toString().trimmed().toUpper();
    const int bdMQty = std::max(0, card.value(u"buydownMultiQty"_s).toString().trimmed().toInt());
    const double bdMAmt = std::max(0.0, card.value(u"buydownMultiAmt"_s).toString().trimmed().toDouble());
    const double bdStart = parseYmd(card.value(u"buydownStart"_s).toString());
    const double bdEnd = parseYmd(card.value(u"buydownEnd"_s).toString());
    auto applyBd = [&](MenuItem &t, double unitBd) {
        std::erase_if(t.recipe, [](const RecipeLine &r) {
            return r.ingredientId.starts_with("__buydown");
        });
        if (unitBd > 0.0) t.recipe.push_back({"__buydown__", unitBd});
        if (!bdGrp.isEmpty()) t.recipe.push_back({"__buydown_grp__:" + ss(bdGrp), 1.0});
        if (bdMQty > 0 && bdMAmt > 0.0) {
            t.recipe.push_back({"__buydown_multi_qty__", double(bdMQty)});
            t.recipe.push_back({"__buydown_multi_amt__", bdMAmt});
        }
        if (bdStart >= 19700101.0) t.recipe.push_back({"__buydown_start__", bdStart});
        if (bdEnd >= 19700101.0) t.recipe.push_back({"__buydown_end__", bdEnd});
    };
    applyBd(item, bdAmt);
    std::string pUpc; double pSz = 10.0;
    for (const RecipeLine &r : item.recipe)
        if (r.ingredientId.starts_with("__parent__:")) { pUpc = r.ingredientId.substr(11); pSz = std::max(1.0, r.quantity); }
    const double packBd = !pUpc.empty() ? bdAmt : (bdAmt / 10.0);
    if (!pUpc.empty() && bdAmt > 0.0) {
        for (MenuItem &pm : menu) if (std::ranges::find(pm.barcodes, pUpc) != pm.barcodes.end()) applyBd(pm, std::round(bdAmt * pSz * 100.0) / 100.0);
    }
    if (!bdGrp.isEmpty() && card.value(u"broadcastBuydownGroup"_s).toBool()) {
        const std::string gTag = "__buydown_grp__:" + ss(bdGrp);
        for (MenuItem &gm : menu) if (gm.id != item.id && std::ranges::any_of(gm.recipe, [&](const RecipeLine &r){ return r.ingredientId == gTag; })) {
            bool isCh = std::ranges::any_of(gm.recipe, [](const RecipeLine &r){ return r.ingredientId.starts_with("__parent__:"); });
            applyBd(gm, isCh ? packBd : std::round(packBd * 10.0 * 100.0) / 100.0);
        }
    }
}
