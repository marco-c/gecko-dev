/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { openTrustedLink } = require("devtools/client/shared/link");
const PropTypes = require("devtools/client/shared/vendor/react-prop-types");
const {
  createFactory,
  Component,
} = require("devtools/client/shared/vendor/react");
const {
  a,
  article,
  footer,
  h1,
  ul,
} = require("devtools/client/shared/vendor/react-dom-factories");
const Worker = createFactory(require("./Worker"));

const FluentReact = require("devtools/client/shared/vendor/fluent-react");
const Localized = createFactory(FluentReact.Localized);

/**
 * This component handles the list of service workers displayed in the application panel
 * and also displays a suggestion to use about debugging for debugging other service
 * workers.
 */
class WorkerList extends Component {
  static get propTypes() {
    return {
      canDebugWorkers: PropTypes.bool.isRequired,
      client: PropTypes.object.isRequired,
      workers: PropTypes.array.isRequired,
    };
  }

  render() {
    const { canDebugWorkers, client, workers } = this.props;

    return [
      article(
        { className: "workers-container" },
        Localized(
          { id: "serviceworker-list-header" },
          h1({
            className: "application--title",
          })
        ),
        ul(
          {},
          workers.map(worker =>
            Worker({
              client,
              isDebugEnabled: canDebugWorkers,
              worker,
            })
          )
        )
      ),
      Localized(
        {
          id: "serviceworker-list-aboutdebugging",
          a: a({
            className: "aboutdebugging-plug__link",
            onClick: () => openTrustedLink("about:debugging#workers"),
          }),
        },
        footer({ className: "aboutdebugging-plug" })
      ),
    ];
  }
}

// Exports

module.exports = WorkerList;
