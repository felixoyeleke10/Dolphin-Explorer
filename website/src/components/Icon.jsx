import React from "react";

function getSvgProps(className, size, title) {
  return {
    className: className ? `icon ${className}` : "icon",
    width: size,
    height: size,
    viewBox: "0 0 24 24",
    fill: "none",
    xmlns: "http://www.w3.org/2000/svg",
    "aria-hidden": title ? undefined : "true",
    role: title ? "img" : undefined,
  };
}

export default function Icon({
  name,
  className = "",
  size = 16,
  title,
}) {
  switch (name) {
    case "arrow-right":
      return (
        <svg {...getSvgProps(className, size, title)}>
          {title ? <title>{title}</title> : null}
          <path d="M5 12H19" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" />
          <path
            d="M12 5L19 12L12 19"
            stroke="currentColor"
            strokeWidth="1.75"
            strokeLinecap="round"
            strokeLinejoin="round"
          />
        </svg>
      );
    case "arrow-up-right":
      return (
        <svg {...getSvgProps(className, size, title)}>
          {title ? <title>{title}</title> : null}
          <path
            d="M8 16L16.5 7.5"
            stroke="currentColor"
            strokeWidth="1.75"
            strokeLinecap="round"
          />
          <path
            d="M10 7H17V14"
            stroke="currentColor"
            strokeWidth="1.75"
            strokeLinecap="round"
            strokeLinejoin="round"
          />
        </svg>
      );
    case "download":
      return (
        <svg {...getSvgProps(className, size, title)}>
          {title ? <title>{title}</title> : null}
          <path d="M12 4V14" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" />
          <path
            d="M8.5 10.5L12 14L15.5 10.5"
            stroke="currentColor"
            strokeWidth="1.75"
            strokeLinecap="round"
            strokeLinejoin="round"
          />
          <path d="M5 18H19" stroke="currentColor" strokeWidth="1.75" strokeLinecap="round" />
        </svg>
      );
    case "mail":
      return (
        <svg {...getSvgProps(className, size, title)}>
          {title ? <title>{title}</title> : null}
          <path
            d="M4.5 7.5H19.5V16.5H4.5V7.5Z"
            stroke="currentColor"
            strokeWidth="1.75"
            strokeLinejoin="round"
          />
          <path
            d="M5 8L12 13L19 8"
            stroke="currentColor"
            strokeWidth="1.75"
            strokeLinecap="round"
            strokeLinejoin="round"
          />
        </svg>
      );
    case "github":
      return (
        <svg
          {...getSvgProps(className, size, title)}
          viewBox="0 0 24 24"
          fill="currentColor"
        >
          {title ? <title>{title}</title> : null}
          <path d="M12 2C6.48 2 2 6.58 2 12.22C2 16.73 4.87 20.56 8.84 21.91C9.34 22.01 9.52 21.69 9.52 21.42C9.52 21.18 9.51 20.39 9.5 19.35C6.73 19.97 6.14 18.15 6.14 18.15C5.68 16.95 5.03 16.63 5.03 16.63C4.12 15.99 5.1 16 5.1 16C6.11 16.07 6.64 17.06 6.64 17.06C7.54 18.65 8.99 18.19 9.56 17.92C9.66 17.24 9.91 16.78 10.19 16.52C7.98 16.26 5.66 15.37 5.66 11.4C5.66 10.27 6.05 9.35 6.7 8.62C6.6 8.36 6.25 7.29 6.8 5.84C6.8 5.84 7.64 5.56 9.5 6.86C10.31 6.63 11.18 6.52 12.05 6.52C12.92 6.52 13.79 6.63 14.6 6.86C16.46 5.56 17.3 5.84 17.3 5.84C17.85 7.29 17.5 8.36 17.4 8.62C18.05 9.35 18.44 10.27 18.44 11.4C18.44 15.38 16.11 16.25 13.9 16.51C14.25 16.82 14.56 17.44 14.56 18.39C14.56 19.75 14.55 20.98 14.55 21.42C14.55 21.69 14.73 22.02 15.24 21.91C19.21 20.56 22.08 16.73 22.08 12.22C22.08 6.58 17.6 2 12 2Z" />
        </svg>
      );
    default:
      return null;
  }
}
