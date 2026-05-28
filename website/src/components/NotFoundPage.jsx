import React from "react";
import { PageBanner } from "./SiteSections.jsx";

export default function NotFoundPage() {
  return (
    <div className="page-stack">
      <PageBanner
        eyebrow="404"
        title="That page does not exist."
        summary="The route you opened is not part of the current Mensor website."
        asideEyebrow="Next step"
        asideTitle="Return to the main site"
        asideBody="Use the main navigation, go back home, or open the research page."
        actions={[
          { label: "Go home", href: "/", solid: true },
          { label: "Open research", href: "/research" },
        ]}
      />
    </div>
  );
}
