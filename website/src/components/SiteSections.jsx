import React from "react";
import Icon from "./Icon.jsx";
import CardVisual from "./CardVisual.jsx";

function getActionIconName(action) {
  if (action.download) return "download";
  if (action.href?.startsWith("mailto:")) return "mail";
  if (action.external) return "arrow-up-right";
  return null;
}

function truncateWords(text, maxWords) {
  if (!maxWords) return text;
  const words = text.trim().split(/\s+/);
  if (words.length <= maxWords) return text;
  return `${words.slice(0, maxWords).join(" ")}...`;
}

export function PageBanner({
  eyebrow,
  title,
  summary,
  actions = [],
  asideArt,
  asideCaption,
}) {
  return (
    <section className="page-banner">
      <div className="page-banner__content">
        {eyebrow && <p className="section-kicker">{eyebrow}</p>}
        <h1>{title}</h1>
        {summary && <p className="page-banner__summary">{summary}</p>}

        {!!actions.length && (
          <div className="page-banner__actions">
            {actions.map((action) => {
              const iconName = getActionIconName(action);
              return (
                <a
                  key={action.label}
                  className={action.solid ? "action-link action-link--solid" : "action-link"}
                  href={action.href}
                  download={action.download || undefined}
                  target={action.external ? "_blank" : undefined}
                  rel={action.external ? "noreferrer" : undefined}
                >
                  {action.label}
                  {iconName ? <Icon name={iconName} className="action-link__icon" /> : null}
                </a>
              );
            })}
          </div>
        )}
      </div>

      {asideArt && (
        <div className="page-banner__art">
          <CardVisual art={asideArt} className="page-banner__art-visual" />
          {asideCaption && <p className="page-banner__art-caption">{asideCaption}</p>}
        </div>
      )}
    </section>
  );
}

export function StoryGrid({ stories, summaryWordLimit }) {
  return (
    <div className="story-grid">
      {stories.map((story) => (
        <article key={story.id} className={`story-card story-card--${story.tone}`}>
          <CardVisual art={story.art} className="story-card__media" />
          <div className="story-card__body">
            <p className="story-card__meta">{`${story.label} / ${story.readTime}`}</p>
            <h3>{story.title}</h3>
            <p>{truncateWords(story.summary, summaryWordLimit)}</p>
            <a className="link-arrow card-link" href={story.link}>
              {story.linkLabel}
            </a>
          </div>
        </article>
      ))}
    </div>
  );
}

export function UpdateGrid({ updates, ctaHref = "/research", ctaLabel = "Read research", bodyWordLimit }) {
  return (
    <div className="update-grid">
      {updates.map((update) => (
        <article key={update.title} className="update-card">
          <CardVisual art={update.art} className="update-card__media" />
          <p className="update-card__kicker">{update.kicker}</p>
          <h3>{update.title}</h3>
          <p>{truncateWords(update.body, bodyWordLimit)}</p>
          <a className="link-arrow card-link" href={ctaHref}>
            {ctaLabel}
          </a>
        </article>
      ))}
    </div>
  );
}

export function DetailGrid({ items, ctaHref = "/about", ctaLabel = "Read more", bodyWordLimit }) {
  return (
    <div className="detail-grid">
      {items.map((item) => (
        <article key={item.title} className="detail-card">
          <CardVisual art={item.art} className="detail-card__media" />
          <p className="detail-card__kicker">{item.kicker}</p>
          <h3>{item.title}</h3>
          <p>{truncateWords(item.body, bodyWordLimit)}</p>
          <a className="link-arrow card-link" href={ctaHref}>
            {ctaLabel}
          </a>
        </article>
      ))}
    </div>
  );
}
