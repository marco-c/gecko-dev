/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { Component } = require("devtools/client/shared/vendor/react");
const dom = require("devtools/client/shared/vendor/react-dom-factories");
const PropTypes = require("devtools/client/shared/vendor/react-prop-types");

/**
 * Renders the "MaskBit" column of a WebSocket frame.
 */
class FrameListColumnMaskBit extends Component {
  static get propTypes() {
    return {
      item: PropTypes.object.isRequired,
      index: PropTypes.number.isRequired,
    };
  }

  shouldComponentUpdate(nextProps) {
    return this.props.item.maskBit !== nextProps.item.maskBit;
  }

  render() {
    const { maskBit } = this.props.item;
    const { index } = this.props;

    return dom.td(
      {
        key: index,
        className: "ws-frames-list-column ws-frames-list-maskBit",
        title: maskBit.toString(),
      },
      maskBit.toString()
    );
  }
}

module.exports = FrameListColumnMaskBit;
