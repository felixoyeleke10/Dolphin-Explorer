import React from "react";
import { DetailGrid, PageBanner } from "./SiteSections.jsx";

export default function DolphinExplorerPage({
  productDetails,
  productHighlights,
}) {
  return (
    <div className="page-stack">
      <PageBanner
        eyebrow="Software"
        title="Dolphin Explorer"
        summary="Windows desktop software for importing, caching, mapping, and reviewing marine survey data across sidescan and sub-bottom workflows."
        asideArt={{ pattern: "panel", scheme: "sunset", label: "Dolphin Explorer" }}
        asideCaption="Desktop product"
        actions={[
          {
            label: "Download app",
            href: "/downloads/DolphinExplorer.exe",
            download: true,
            solid: true,
          },
          { label: "Contact the lab", href: "/contact" },
        ]}
      />

      <section className="section product-section">
        <div className="product-section__copy">
          <p className="section-kicker">Platform</p>
          <h2>Survey software built around durable project state</h2>
          <p>Dolphin Explorer keeps raw sources, parsed caches, map layers, metadata, and processing graphs connected so operators can reopen work without starting from zero.</p>
        </div>

        <div className="product-section__panel">
          <p className="product-section__label">Capabilities</p>
          <ul>
            {productHighlights.map((item) => (
              <li key={item}>{item}</li>
            ))}
          </ul>
        </div>
      </section>

      <section className="section">
        <div className="section__header">
          <p className="section-kicker">Details</p>
          <h2>How the platform is built</h2>
        </div>
        <DetailGrid
          items={productDetails}
          ctaHref="/contact"
          ctaLabel="Get in touch"
          bodyWordLimit={16}
        />
      </section>
    </div>
  );
}
