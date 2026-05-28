import React from "react";
import { DetailGrid, PageBanner, StoryGrid } from "./SiteSections.jsx";

export default function MissionsPage({ featuredStories, missionNotes }) {
  return (
    <div className="page-stack">
      <PageBanner
        eyebrow="Mission archive"
        title="Operational stories from the marine data stack."
        summary="Mission review, event triage, and analyst workflows built for dense survey timelines."
        asideEyebrow="Scope"
        asideTitle="Subsea sensing and analyst systems"
        asideBody="Software, AI assistance, and field reality across marine review work."
        asideArt={{ pattern: "contour", scheme: "seafoam" }}
        asideCaption="Mission systems"
        actions={[
          { label: "Open research", href: "/research" },
          { label: "View Dolphin Explorer", href: "/dolphin-explorer", solid: true },
        ]}
      />

      <section className="section">
        <div className="section__header">
          <p className="section-kicker">Featured</p>
          <h2>Mission stories</h2>
        </div>
        <StoryGrid stories={featuredStories} summaryWordLimit={14} />
      </section>

      <section className="section">
        <div className="section__header">
          <p className="section-kicker">Field notes</p>
          <h2>What matters in practice</h2>
        </div>
        <DetailGrid
          items={missionNotes}
          ctaHref="/dolphin-explorer"
          ctaLabel="View software"
        />
      </section>
    </div>
  );
}
