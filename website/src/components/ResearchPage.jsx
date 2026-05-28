import React from "react";
import { PageBanner, StoryGrid, UpdateGrid } from "./SiteSections.jsx";

export default function ResearchPage({
  featuredStories,
  researchUpdates,
  searchCount,
  searchQuery,
}) {
  const hasResults = featuredStories.length || researchUpdates.length;
  const hasSearch = Boolean(searchQuery.trim());

  return (
    <div className="page-stack">
      <PageBanner
        eyebrow="Research"
        title="Applied AI for subsea interpretation."
        summary="Practical inference, candidate surfacing, and workflow design for marine review."
        asideEyebrow={hasSearch ? "Search active" : "Mission brief"}
        asideTitle={
          hasSearch ? `Results for "${searchQuery.trim()}"` : "Marine-first research"
        }
        asideBody={
          hasSearch
            ? `${searchCount} matching items across research notes and featured mission work.`
            : "Space-aware context matters when it improves marine planning or interpretation."
        }
        asideArt={hasSearch ? { pattern: "timeline", scheme: "steel" } : { pattern: "stack", scheme: "steel" }}
        asideCaption={hasSearch ? "Search results" : "Research stack"}
        actions={[
          { label: "Back to home", href: "/" },
          { label: "Open missions", href: "/missions", solid: true },
        ]}
      />

      {!hasResults && (
        <div className="empty-state">
          <p>No research items matched that search.</p>
        </div>
      )}

      {!!researchUpdates.length && (
        <section className="section">
          <div className="section__header">
            <p className="section-kicker">Updates</p>
            <h2>Research notes</h2>
          </div>
          <UpdateGrid
            updates={researchUpdates}
            ctaHref="/research"
            ctaLabel="Research note"
            bodyWordLimit={16}
          />
        </section>
      )}

      {!!featuredStories.length && (
        <section className="section">
          <div className="section__header">
            <p className="section-kicker">Related missions</p>
            <h2>Work connected to the research stack</h2>
          </div>
          <StoryGrid stories={featuredStories} summaryWordLimit={14} />
        </section>
      )}
    </div>
  );
}
